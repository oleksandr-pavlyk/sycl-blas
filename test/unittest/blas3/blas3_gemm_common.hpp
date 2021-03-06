/***************************************************************************
 *
 *  @license
 *  Copyright (C) Codeplay Software Limited
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  For your convenience, a copy of the License has been included in this
 *  repository.
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  SYCL-BLAS: BLAS implementation using SYCL
 *
 *  @filename blas3_gemm_values.hpp
 *
 **************************************************************************/

#include "blas_test.hpp"

template <typename T>
using gemm_arguments_t =
    std::tuple<int, int, int, int, int, char, char, T, T, int, int, int>;

template <typename scalar_t>
inline void verify_gemm(const gemm_arguments_t<scalar_t> arguments) {
  int offset;
  int batch;
  int m;
  int n;
  int k;
  char transa;
  char transb;
  scalar_t alpha;
  scalar_t beta;
  int lda_mul;
  int ldb_mul;
  int ldc_mul;
  std::tie(offset, batch, m, n, k, transa, transb, alpha, beta, lda_mul,
           ldb_mul, ldc_mul) = arguments;

  const char ta_str[2] = {transa, '\0'};
  const char tb_str[2] = {transb, '\0'};

  auto q = make_queue();
  test_executor_t ex(q);

  auto policy_handler = ex.get_policy_handler();

  const int lda = ((transa != 'n') ? k : m) * lda_mul;
  const int ldb = ((transb != 'n') ? n : k) * ldb_mul;
  const int ldc = m * ldc_mul;

  const int size_a = m * k * lda_mul;
  const int size_b = k * n * ldb_mul;
  const int size_c = m * n * ldc_mul;

  const int buffer_size_a = batch * size_a + offset;
  const int buffer_size_b = batch * size_b + offset;
  const int buffer_size_c = batch * size_c + offset;

  std::vector<scalar_t> a_m(buffer_size_a);
  std::vector<scalar_t> b_m(buffer_size_b);
  std::vector<scalar_t> c_m_gpu(buffer_size_c);

  fill_random(a_m);
  fill_random(b_m);
  fill_random(c_m_gpu);
  std::vector<scalar_t> c_m_cpu = c_m_gpu;

  // Use system blas to create a reference output
  for (int i = 0; i < batch; ++i) {
    reference_blas::gemm(ta_str, tb_str, m, n, k, alpha,
                         a_m.data() + i * size_a + offset, lda,
                         b_m.data() + i * size_b + offset, ldb, beta,
                         c_m_cpu.data() + i * size_c + offset, ldc);
  }

  auto m_a_gpu = blas::make_sycl_iterator_buffer<scalar_t>(buffer_size_a);
  auto m_b_gpu = blas::make_sycl_iterator_buffer<scalar_t>(buffer_size_b);
  auto m_c_gpu = blas::make_sycl_iterator_buffer<scalar_t>(buffer_size_c);

  policy_handler.copy_to_device(a_m.data(), m_a_gpu, buffer_size_a);
  policy_handler.copy_to_device(b_m.data(), m_b_gpu, buffer_size_b);
  policy_handler.copy_to_device(c_m_gpu.data(), m_c_gpu, buffer_size_c);

  // SYCL BLAS GEMM implementation
  if (batch == 1) {
    _gemm(ex, transa, transb, m, n, k, alpha, m_a_gpu + offset, lda,
          m_b_gpu + offset, ldb, beta, m_c_gpu + offset, ldc);
  } else {
    _gemm_batched(ex, transa, transb, m, n, k, alpha, m_a_gpu + offset, lda,
                  m_b_gpu + offset, ldb, beta, m_c_gpu + offset, ldc, batch);
  }

  auto event =
      policy_handler.copy_to_host(m_c_gpu, c_m_gpu.data(), buffer_size_c);
  policy_handler.wait(event);

  ASSERT_TRUE(utils::compare_vectors(c_m_gpu, c_m_cpu));
  ex.get_policy_handler().wait();
}

#define GENERATE_GEMM_TEST_IMPL(TESTSUITE, TESTNAME, DTYPE, COMBINATION)      \
  class TESTNAME : public ::testing::TestWithParam<gemm_arguments_t<DTYPE>> { \
  };                                                                          \
  TEST_P(TESTNAME, test) { verify_gemm<DTYPE>(GetParam()); };                 \
  INSTANTIATE_TEST_SUITE_P(TESTSUITE, TESTNAME, COMBINATION)

#define GENERATE_GEMM_WITH_DTYPE(TESTSUITE, DTYPE, COMBINATION) \
  GENERATE_GEMM_TEST_IMPL(TESTSUITE, TESTSUITE##COMBINATION, DTYPE, COMBINATION)

#define GENERATE_GEMM_FLOAT(TESTSUITE, COMBINATION) \
  GENERATE_GEMM_WITH_DTYPE(TESTSUITE, float, COMBINATION)

#ifdef DOUBLE_SUPPORT
#define GENERATE_GEMM_DOUBLE(TESTSUITE, COMBINATION) \
  GENERATE_GEMM_WITH_DTYPE(TESTSUITE, double, COMBINATION)
#else
#define GENERATE_GEMM_DOUBLE(TESTSUITE, COMBINATION)
#endif  // DOUBLE_SUPPORT

#define GENERATE_GEMM_TEST(TESTSUITE, COMBINATION) \
  GENERATE_GEMM_FLOAT(TESTSUITE, COMBINATION);     \
  GENERATE_GEMM_DOUBLE(TESTSUITE, COMBINATION)

