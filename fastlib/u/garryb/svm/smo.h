	#ifndef U_SVM_SMO_H
#define U_SVM_SMO_H

#include "fastlib/fastlib.h"

/* TODO: I don't actually want these to be public */
const double SMO_ZERO = 1.0e-8;
const double SMO_EPS = 1.0e-4;
const double SMO_TOLERANCE = 1.0e-4;

template<typename TKernel>
class SMO {
  FORBID_COPY(SMO);

 public:
  typedef TKernel Kernel;

 private:
  Matrix kernel_cache_sign_;
  Kernel kernel_;
  const Dataset *dataset_;
  index_t n_data_;
  Matrix matrix_;
  Vector alpha_;
  Vector error_;
  double thresh_;
  double c_;
  double b_;
  double sum_alpha_;

 public:
  SMO() {}
  ~SMO() {}

  /**
   * Initializes an SMO problem.
   *
   * You must initialize separately the kernel.
   */
  void Init(const Dataset* dataset_in, double c_in, double b_in) {
    c_ = c_in;
    b_ = b_in;

    dataset_ = dataset_in;
    matrix_.Alias(dataset_->matrix());
    
    n_data_ = matrix_.n_cols();

    alpha_.Init(n_data_);
    alpha_.SetZero();
    sum_alpha_ = 0;

    error_.Init(n_data_);
    error_.SetZero();

    thresh_ = 0;
  }

  void Train();

  const Kernel& kernel() const {
    return kernel_;
  }

  Kernel& kernel() {
    return kernel_;
  }

  double threshold() const {
    return thresh_;
  }

  void GetSVM(Matrix *support_vectors, Vector *support_alpha) const;

 private:
  index_t TrainIteration_(bool examine_all);

  bool TryChange_(index_t j);

  bool TakeStep_(index_t i, index_t j, double error_j);

  double FixAlpha_(double alpha) const {
    if (alpha < SMO_ZERO) {
      alpha = 0;
    } else if (alpha > c_ - SMO_ZERO) {
      alpha = c_;
    }
    return alpha;
  }

  bool IsBound_(double alpha) const {
    return alpha <= 0 || alpha >= c_;
  }

  int GetLabelSign_(index_t i) const {
    return matrix_.get(matrix_.n_rows()-1, i) != 0 ? 1 : -1;
  }

  void GetVector_(index_t i, Vector *v) const {
    matrix_.MakeColumnSubvector(i, 0, matrix_.n_rows()-1, v);
  }

  double Error_(index_t i) const {
    double val;
    if (!IsBound_(alpha_[i])) {
      val = error_[i];
      DEBUG_MSG(0, "error values %f and %f", error_[i], Evaluate_(i) - GetLabelSign_(i));
    } else {
      val = Evaluate_(i) - GetLabelSign_(i);
    }
    return val;
  }

  double Evaluate_(index_t i) const;

  double EvalKernel_(index_t i, index_t j) const {
    return kernel_cache_sign_.get(i, j) * (GetLabelSign_(i) * GetLabelSign_(j));
  }
  
  void CalcKernels_() {
    kernel_cache_sign_.Init(n_data_, n_data_);
    fprintf(stderr, "Kernel Start\n");
    for (index_t i = 0; i < n_data_; i++) {
      for (index_t j = 0; j < n_data_; j++) {
        Vector v_i;
        GetVector_(i, &v_i);
        Vector v_j;
        GetVector_(j, &v_j);
        double k = kernel_.Eval(v_i, v_j);
        
        kernel_cache_sign_.set(j, i, k * GetLabelSign_(i) * GetLabelSign_(j));
      }
    }
    fprintf(stderr, "Kernel Stop\n");
  }
};

template<typename TKernel>
void SMO<TKernel>::GetSVM(Matrix *support_vectors, Vector *support_alpha) const {
  index_t n_support = 0;
  index_t i_support = 0;

  for (index_t i = 0; i < n_data_; i++) {
    if (unlikely(alpha_[i] != 0)) {
      n_support++;
    }
  }

  support_vectors->Init(matrix_.n_rows() - 1, n_support);
  support_alpha->Init(n_support);

  for (index_t i = 0; i < n_data_; i++) {
    if (unlikely(alpha_[i] != 0)) {
      Vector source;
      Vector dest;

      GetVector_(i, &source);
      support_vectors->MakeColumnVector(i_support, &dest);
      dest.CopyValues(source);

      (*support_alpha)[i_support] = alpha_[i] * GetLabelSign_(i);

      i_support++;
    }
  }
}

template<typename TKernel>
double SMO<TKernel>::Evaluate_(index_t i) const {
  // TODO: This only handles linear
  Vector kernel_values;
  double summation = 0;
  
  kernel_cache_sign_.MakeColumnVector(i, &kernel_values);
  summation = la::Dot(alpha_, kernel_values) * GetLabelSign_(i);
  
  return (summation - thresh_);
}

template<typename TKernel>
void SMO<TKernel>::Train() {
  bool examine_all = true;
  index_t num_changed = 0;
  
  CalcKernels_();

  while (num_changed > 0 || examine_all) {
    DEBUG_GOT_HERE(0);
    num_changed = TrainIteration_(examine_all);

    if (examine_all) {
      examine_all = false;
    } else if (num_changed == 0) {
      examine_all = true;
    }
  }
}

template<typename TKernel>
index_t SMO<TKernel>::TrainIteration_(bool examine_all) {
  index_t num_changed = 0;

  for (index_t i = 0; i < n_data_; i++) {
    if ((examine_all || !IsBound_(alpha_[i])) && TryChange_(i)) {
      num_changed++;
    }
  }

  return num_changed;
}

template<typename TKernel>
bool SMO<TKernel>::TryChange_(index_t j) {
  double error_j = Error_(j);
  double rj = error_j * GetLabelSign_(j);

  DEBUG_GOT_HERE(0);

  if (!((rj < -SMO_TOLERANCE && alpha_[j] < c_)
      || (rj > SMO_TOLERANCE && alpha_[j] > 0))) {
    return false; // nothing to change
  }

  // first try the one we suspect to have the largest yield

  if (error_j != 0) {
    index_t i = -1;
    
#if 0
#else    
    double diff_max = 0;
    
    for (index_t k = 0; k < n_data_; k++) {
      if (!IsBound_(alpha_[k])) {
        double error_k = error_[k];
        double diff_k = fabs(error_k - error_j);
        if (unlikely(diff_k > diff_max)) {
          diff_max = diff_k;
          i = k;
        }
      }
    }
#endif
    if (i != -1 && TakeStep_(i, j, error_j)) {
      return true;
    }
  }

  DEBUG_GOT_HERE(0);
  // try searching through non-bound examples
  index_t start_i = rand() % n_data_;
  index_t i = start_i;

  do {
    //if (alpha_[i] != 0 && TakeStep_(i, j, error_j)) {
    if (!IsBound_(alpha_[i]) && TakeStep_(i, j, error_j)) {
      return true;
    }
    i = (i + 1) % n_data_;
  } while (i != start_i);

  DEBUG_GOT_HERE(0);
  // try searching through all examples
  start_i = rand() % n_data_;
  i = start_i;

  do {
    if (IsBound_(alpha_[i]) && TakeStep_(i, j, error_j)) {
      return true;
    }
    i = (i + 1) % n_data_;
  } while (i != start_i);

  return false;
}

template<typename TKernel>
bool SMO<TKernel>::TakeStep_(index_t i, index_t j, double error_j) {
  if (i == j) {
    DEBUG_GOT_HERE(0);
    return false;
  }

  int yi = GetLabelSign_(i);
  int yj = GetLabelSign_(j);
  double alpha_i = alpha_[i];
  double alpha_j = alpha_[j];
  double delta_thresh;
  double l;
  double u;
  int s = (yi == yj) ? 1 : -1;
  double error_i = Error_(i);
  double r;
  double budget_upper_bound;
  
  if (s < 0) {
    DEBUG_ASSERT(s == -1);
    r = alpha_j - alpha_i; // target values are not equal
    double gamma = alpha_i - alpha_j;
    budget_upper_bound = (gamma - b_*c_ + sum_alpha_ - alpha_i - alpha_j) / (-2);
  } else {
    r = alpha_j + alpha_i - c_; // target values are equal
    budget_upper_bound = DBL_MAX;
  }
  
  l = math::ClampNonNegative(r);
  u = min(c_ + math::ClampNonPositive(r), budget_upper_bound);

  if (l == u) {
    // TODO: might put in some tolerance
    DEBUG_MSG(0, "l=%f, u=%f, r=%f, c_=%f, s=%f", l, u, r, c_, s);
    DEBUG_GOT_HERE(0);
    return false;
  }

  // cached kernel values
  double kii = EvalKernel_(i, i);
  double kij = EvalKernel_(i, j);
  double kjj = EvalKernel_(j, j);
  // second derivative of objective function
  double eta = +2*kij - kii - kjj;

  DEBUG_MSG(0, "kij=%f, kii=%f, kjj=%f", kij, kii, kjj);

  if (likely(eta < 0)) {
    DEBUG_MSG(0, "Common case");
    alpha_j = alpha_[j] - yj * (error_i - error_j) / eta;
    alpha_j = math::ClampRange(alpha_j, l, u);
  } else {
    DEBUG_MSG(0, "Uncommon case");
    //abort();
    double c1 = eta/2;
    double c2 = yj * (error_i - error_j) - eta * alpha_j;
    double objlower = c1*l*l + c2*l;
    double objupper = c1*u*u + c2*u;
    
    if (objlower > objupper + SMO_EPS) {
      alpha_j = l;
    } else if (objlower < objupper - SMO_EPS) {
      alpha_j = u;
    } else {
      alpha_j = alpha_[j];
    }
  }

  alpha_j = FixAlpha_(alpha_j);

  double delta_alpha_j = alpha_j - alpha_[j];

  // check if there is progress
  if (fabs(delta_alpha_j) < SMO_EPS*(alpha_j + alpha_[j] + SMO_EPS)) {
    DEBUG_GOT_HERE(0);
    return false;
  }

  alpha_i = alpha_i - (s)*(delta_alpha_j);
  if (alpha_i < SMO_ZERO) {
    alpha_j += s * alpha_i;
    alpha_i = 0;
    //abort();
  } else if (alpha_i > c_ - SMO_ZERO) {
    double t = alpha_i - c_;
    alpha_j += s * t;
    alpha_i = c_;
    //abort();
  }
  alpha_j = FixAlpha_(alpha_j);
  double delta_alpha_i = alpha_i - alpha_[i];

  // calculate threshold
  double delta_thresh_i = error_i + yi*delta_alpha_i*kii + yj*delta_alpha_j*kij;
  double delta_thresh_j = error_j + yi*delta_alpha_i*kij + yj*delta_alpha_j*kjj;

  if (!IsBound_(alpha_i)) {
    delta_thresh = delta_thresh_i;
  } else if (!IsBound_(alpha_j)) {
    delta_thresh = delta_thresh_j;
  } else {
    delta_thresh = (delta_thresh_i + delta_thresh_j) / 2.0;
  }

  Vector kernel_i;
  Vector kernel_j;
  
  kernel_cache_sign_.MakeColumnVector(i, &kernel_i);
  kernel_cache_sign_.MakeColumnVector(j, &kernel_j);

  for (index_t k = 0; k < n_data_; k++) {
    if (likely(k != i) && likely(k != j) && !IsBound_(alpha_[k])) {
      error_[k] += (delta_alpha_i*kernel_i[k] + delta_alpha_j*kernel_j[k]) * GetLabelSign_(k) - delta_thresh;
    }
  }

  thresh_ += delta_thresh;

  alpha_[i] = alpha_i;
  alpha_[j] = alpha_j;
  sum_alpha_ += delta_alpha_i + delta_alpha_j;

  // this is only necessary when i or j are not bound, but there is nothing
  // wrong with doing this all the time
  error_[i] = 0;
  error_[j] = 0;

  DEBUG_GOT_HERE(0);
  return true;
}

#endif
