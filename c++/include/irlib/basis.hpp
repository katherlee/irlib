#pragma once

#include <iostream>
#include <complex>
#include <cmath>
#include <vector>
#include <set>
#include <assert.h>

#include <Eigen/CXX11/Tensor>

#include <boost/multi_array.hpp>
#include <boost/type_traits.hpp>
#include <boost/shared_ptr.hpp>

#include "piecewise_polynomial.hpp"

#include "common.hpp"
#include "detail/aux.hpp"
#include "detail/spline.hpp"

namespace irlib {
    /***
     * Construct a piecewise polynomial by means of cubic spline
     * @param T  we expect T=double
     * @param x_array  values of x in strictly ascending order
     * @param y_array  values of y
     */


    /**
     * Abstract class representing an analytical continuation kernel
     */
    template<typename T>
    class kernel {
    public:
        virtual ~kernel() {};

        /// return the value of the kernel for given x and y in the [-1,1] interval.
        virtual T operator()(double x, double y) const = 0;

        /// return statistics
        virtual irlib::statistics::statistics_type get_statistics() const = 0;

        /// return lambda
        virtual double Lambda() const = 0;

#ifndef SWIG

        /// return a reference to a copy
        virtual boost::shared_ptr<kernel> clone() const = 0;

#endif
    };

#ifdef SWIG
    %template(real_kernel) kernel<double>;
#endif

    /**
     * Fermionic kernel
     */
    class fermionic_kernel : public kernel<double> {
    public:
        fermionic_kernel(double Lambda) : Lambda_(Lambda) {}

        virtual ~fermionic_kernel() {};

        double operator()(double x, double y) const {
            const double limit = 100.0;
            if (Lambda_ * y > limit) {
                return std::exp(-0.5 * Lambda_ * x * y - 0.5 * Lambda_ * y);
            } else if (Lambda_ * y < -limit) {
                return std::exp(-0.5 * Lambda_ * x * y + 0.5 * Lambda_ * y);
            } else {
                return std::exp(-0.5 * Lambda_ * x * y) / (2 * std::cosh(0.5 * Lambda_ * y));
            }
        }

        irlib::statistics::statistics_type get_statistics() const {
            return irlib::statistics::FERMIONIC;
        }

        double Lambda() const {
            return Lambda_;
        }

#ifndef SWIG

        boost::shared_ptr<kernel> clone() const {
            return boost::shared_ptr<kernel>(new fermionic_kernel(Lambda_));
        }

#endif

    private:
        double Lambda_;
    };

    /**
     * Bosonic kernel
     */
    class bosonic_kernel : public kernel<double> {
    public:
        bosonic_kernel(double Lambda) : Lambda_(Lambda) {}

        virtual ~bosonic_kernel() {};

        double operator()(double x, double y) const {
            const double limit = 100.0;
            if (std::abs(Lambda_ * y) < 1e-10) {
                return std::exp(-0.5 * Lambda_ * x * y) / Lambda_;
            } else if (Lambda_ * y > limit) {
                return y * std::exp(-0.5 * Lambda_ * x * y - 0.5 * Lambda_ * y);
            } else if (Lambda_ * y < -limit) {
                return -y * std::exp(-0.5 * Lambda_ * x * y + 0.5 * Lambda_ * y);
            } else {
                return y * std::exp(-0.5 * Lambda_ * x * y) / (2 * std::sinh(0.5 * Lambda_ * y));
            }
        }

        irlib::statistics::statistics_type get_statistics() const {
            return irlib::statistics::BOSONIC;
        }

        double Lambda() const {
            return Lambda_;
        }

#ifndef SWIG

        boost::shared_ptr<kernel> clone() const {
            return boost::shared_ptr<kernel>(new bosonic_kernel(Lambda_));
        }

#endif

    private:
        double Lambda_;
    };

/**
 * Class template for kernel Ir basis
 * @tparam Scalar scalar type
 */
    template<typename Scalar>
    class ir_basis_set {
    public:
        /**
         * Constructor
         * @param knl  kernel
         * @param max_dim  max number of basis functions computed.
         * @param cutoff  we drop basis functions corresponding to small singular values  |s_l/s_0~ < cutoff.
         * @param N       dimension of matrices for SVD. 500 may be big enough al least up to Lambda = 10^4.
         */
        ir_basis_set(const kernel<Scalar> &knl, int max_dim, double cutoff = 1e-10, int N = 501) throw(std::runtime_error);

    private:
        boost::shared_ptr<kernel<Scalar> > p_knl_;
        std::vector< irlib::piecewise_polynomial<double> > basis_functions_;

    public:
        /**
         * Compute the values of the basis functions for a given x.
         * @param x    x = 2 * tau/beta - 1  (-1 <= x <= 1)
         * @param val  results
         */
#ifndef SWIG
        void values(double x, std::vector<double> &val) const throw(std::runtime_error);
#endif

        double value(double x, int l) const throw(std::runtime_error) {
            assert(x >= -1.00001 && x <= 1.00001);
            assert(l >= 0 && l < dim());
            if (l < 0 || l >= dim()) {
                throw std::runtime_error("Invalid index of basis function!");
            }
            if (x < -1 || x > 1) {
                throw std::runtime_error("Invalid value of x!");
            }

            return basis_functions_[l].compute_value(x);
        }

        std::vector<double> values(double x) const throw(std::runtime_error) {
            if (x < -1 || x > 1) {
                throw std::runtime_error("Invalid value of x!");
            }
            std::vector<double> val;
            values(x, val);
            return val;
        }

        /**
         * Return a reference to the l-th basis function
         * @param l l-th basis function
         * @return  reference to the l-th basis function
         */
        const irlib::piecewise_polynomial<double> &operator()(int l) const throw(std::runtime_error) { return basis_functions_[l]; }

        /**
         * Return a reference to all basis functions
         */
        //const std::vector<irlib::piecewise_polynomial<double> > all() const { return basis_functions_; }

        /**
         * Return number of basis functions
         * @return  number of basis functions
         */
        int dim() const { return basis_functions_.size(); }

        /// Return statistics
        irlib::statistics::statistics_type get_statistics() const {
            return p_knl_->get_statistics();
        }

        /**
         * Compute transformation matrix to Matsubara freq.
         * The computation may take some time. You may store the result somewhere and do not call this routine frequenctly.
         * @param n_min min Matsubara freq. index
         * @param n_max max Matsubara freq. index
         * @param Tnl max
         */
#ifndef SWIG

        void compute_Tnl(
                int n_min, int n_max,
                boost::multi_array<std::complex<double>, 2> &Tnl
        ) const;

        void compute_Tnl(
                int n_min, int n_max,
                Eigen::Tensor<std::complex<double>, 2> &Tnl
        ) const;

        void compute_Tnl(
                const std::vector<long> &n_vec,
                Eigen::Tensor<std::complex<double>, 2> &Tnl
        ) const {
            irlib::compute_transformation_matrix_to_matsubara<double>(n_vec,
                                                                                   p_knl_->get_statistics(),
                                                                                   basis_functions_,
                                                                                   Tnl);
        }
#endif

        Eigen::Tensor<std::complex<double>, 2>
        compute_Tnl(const std::vector<long> &n_vec) const {
            Eigen::Tensor<std::complex<double>, 2> Tnl;
            compute_Tnl(n_vec, Tnl);
            return Tnl;
        }

        Eigen::Tensor<std::complex<double>, 2>
        compute_Tbar_ol(const std::vector<long> &o_vec) const {
            int no = o_vec.size();
            int nl = basis_functions_.size();

            Eigen::Tensor<std::complex<double>, 2> Tbar_ol(no, nl);
            irlib::compute_Tbar_ol(o_vec, basis_functions_, Tbar_ol);

            return Tbar_ol;
        }


    };

#ifdef SWIG
    %template(real_ir_basis_set) ir_basis_set<double>;
#endif

    /**
     * Fermionic IR basis
     */
    class basis_f : public ir_basis_set<double> {
    public:
        basis_f(double Lambda, int max_dim, double cutoff = 1e-10, int N = 501) throw(std::runtime_error)
                : ir_basis_set<double>(fermionic_kernel(Lambda), max_dim, cutoff, N) {}
    };

    /**
     * Bosonic IR basis
     */
    class basis_b : public ir_basis_set<double> {
    public:
        basis_b(double Lambda, int max_dim, double cutoff = 1e-10, int N = 501) throw(std::runtime_error)
                : ir_basis_set<double>(bosonic_kernel(Lambda), max_dim, cutoff, N) {}
    };
}

#include "detail/ir_basis.ipp"
