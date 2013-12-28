#include "P2P.hpp"
#include "Util.hpp"
#include "meta/random.hpp"

#include "kernel/Laplace.kern"

#include <iostream>
#include <iomanip>

int main() {
  typedef LaplacePotential kernel_type;
  kernel_type K;

  typedef kernel_type::source_type source_type;
  typedef kernel_type::charge_type charge_type;
  typedef kernel_type::target_type target_type;
  typedef kernel_type::result_type result_type;

  std::cout << "Symmetric Diagonal" << std::endl;
  for (unsigned n = 1; n < 40000; n *= 2) {
    std::vector<source_type> s(n);
    std::vector<charge_type> c(n);
    std::vector<result_type> r(n);

    for (unsigned i = 0; i < n; ++i) {
      s[i] = meta::random<source_type>::get();
      c[i] = meta::random<charge_type>::get();
      r[i] = meta::random<result_type>::get();
    }

    std::vector<result_type> r1 = r;
    Clock timer;
    detail::block_eval(K, s.begin(), s.end(), c.begin(), r1.begin());
    double old_time = timer.elapsed();

    std::vector<result_type> r2 = r;
    timer.start();
    p2p(K, s.begin(), s.end(), c.begin(), r2.begin());
    double new_time = timer.elapsed();

    double error = 0;
    for (unsigned i = 0; i < n; ++i) {
      error += normSq(r2[i] - r1[i]) / normSq(r1[i]);
    }
    error = std::sqrt(error);

    std::cout << std::setw(10) << n << "\t"
              << std::setw(10) << error << "\t"
              << std::setw(10) << old_time << "\t"
              << std::setw(10) << new_time << std::endl;
  }

  std::cout << "Symmetric Off-Diagonal" << std::endl;
  for (unsigned n = 1; n < 80000; n *= 2) {
    std::vector<source_type> s(n);
    std::vector<charge_type> c(n);
    std::vector<result_type> r(n);

    for (unsigned i = 0; i < n; ++i) {
      s[i] = meta::random<source_type>::get();
      c[i] = meta::random<charge_type>::get();
      r[i] = meta::random<result_type>::get();
    }

    std::vector<result_type> r1 = r;
    Clock timer;
    detail::block_eval(K, s.begin(), s.begin()+n/2, c.begin(), r1.begin(),
                          s.begin()+n/2, s.end(), c.begin()+n/2, r1.begin()+n/2);
    double old_time = timer.elapsed();

    std::vector<result_type> r2 = r;
    timer.start();
    p2p(K, s.begin(), s.begin()+n/2, c.begin(), r2.begin(),
           s.begin()+n/2, s.end(), c.begin()+n/2, r2.begin()+n/2);
    double new_time = timer.elapsed();

    double error = 0;
    for (unsigned i = 0; i < n; ++i) {
      error += normSq(r2[i] - r1[i]) / normSq(r1[i]);
    }
    error = std::sqrt(error);

    std::cout << std::setw(10) << n/2 << "\t"
              << std::setw(10) << error << "\t"
              << std::setw(10) << old_time << "\t"
              << std::setw(10) << new_time << std::endl;
  }

  std::cout << std::endl << "Asymmetric off-diagonal" << std::endl;
  for (unsigned n = 1; n < 40000; n *= 2) {
    std::vector<source_type> s(n);
    std::vector<target_type> t(n);
    std::vector<charge_type> c(n);
    std::vector<result_type> r(n);

    for (unsigned i = 0; i < n; ++i) {
      s[i] = meta::random<source_type>::get();
      t[i] = meta::random<target_type>::get();
      c[i] = meta::random<charge_type>::get();
      r[i] = meta::random<result_type>::get();
    }

    std::vector<result_type> r1 = r;
    Clock timer;
    detail::block_eval(K, s.begin(), s.end(), c.begin(),
                          t.begin(), t.end(), r1.begin());
    double old_time = timer.elapsed();

    std::vector<result_type> r2 = r;
    timer.start();
    p2p(K, s.begin(), s.end(), c.begin(),
           t.begin(), t.end(), r2.begin());
    double new_time = timer.elapsed();

    double error = 0;
    for (unsigned i = 0; i < n; ++i) {
      error += normSq(r2[i] - r1[i]) / normSq(r1[i]);
    }
    error = std::sqrt(error);

    std::cout << std::setw(10) << n << "\t"
              << std::setw(10) << error << "\t"
              << std::setw(10) << old_time << "\t"
              << std::setw(10) << new_time << std::endl;
  }

}
