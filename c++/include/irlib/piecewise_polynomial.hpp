#pragma once

#include <complex>
#include <cmath>
#include <vector>
#include <cassert>
#include <type_traits>
#include <mpreal.h>
#include <iomanip>
#include <fstream>

/**
 * @brief Class representing a pieacewise polynomial and utilities
 */
namespace irlib {

    template<typename T, typename Tx>
    class piecewise_polynomial;

    namespace detail {
        /**
         *
         * @tparam T double or std::complex<double>
         * @param a  scalar
         * @param b  scalar
         * @return   conj(a) * b
         */
        template<class T>
        typename std::enable_if<std::is_floating_point<T>::value, T>::type
        outer_product(T a, T b) {
            return a * b;
        }

        inline mpfr::mpreal outer_product(mpfr::mpreal a, mpfr::mpreal b) {
            return a * b;
        }

        template<class T>
        std::complex<T>
        outer_product(const std::complex<T> &a, const std::complex<T> &b) {
            return std::conj(a) * b;
        }

        template<class T>
        typename std::enable_if<std::is_floating_point<T>::value, T>::type
        conjg(T a) {
            return a;
        }

        template<class T>
        std::complex<T>
        conjg(const std::complex<T> &a) {
            return std::conj(a);
        }

        template<typename T, typename Op>
        struct pp_element_wise_op {
            void perform(const T *p1, const T *p2, T *p_r, int k1, int k2, int k_r) const {
                Op op;
                int k = std::min(k1, k2);
                assert(k_r >= k);
                for (int i = 0; i < k_r + 1; ++i) {
                    p_r[i] = 0.0;
                }
                for (int i = 0; i < k + 1; ++i) {
                    p_r[i] = op(p1[i], p2[i]);
                }
            }
        };

        template<typename T>
        struct pp_plus : public pp_element_wise_op<T, std::plus<T> > {
        };

        template<typename T>
        struct pp_minus : public pp_element_wise_op<T, std::minus<T> > {
        };

///  element-Wise operations on piecewise_polynomial coefficients
        template<typename T, typename Tx, typename Op>
        piecewise_polynomial<T,Tx>
        do_op(const piecewise_polynomial<T,Tx> &f1, const piecewise_polynomial<T,Tx> &f2, const Op &op) {
            if (f1.section_edges_ != f2.section_edges_) {
                throw std::runtime_error("Cannot add two numerical functions with different sections!");
            }

            const int k_new = std::max(f1.order(), f2.order());
            piecewise_polynomial<T,Tx> result(k_new, f1.section_edges());

            const int k_min = std::min(f1.order(), f2.order());

            std::vector<T> coeff1(f1.order()+1);
            std::vector<T> coeff2(f2.order()+1);
            std::vector<T> coeff_r(k_new+1);

            for (int s = 0; s < f1.num_sections(); ++s) {
                for (int k=0; k<f1.order()+1; ++k) {
                    coeff1[k] = f1.coeff_(s,k);
                }
                for (int k=0; k<f2.order()+1; ++k) {
                    coeff2[k] = f2.coeff_(s,k);
                }
                op.perform(&coeff1[0], &coeff2[0], &coeff_r[0], f1.order(), f2.order(), k_min);
                for (int k=0; k<k_new+1; ++k) {
                    result.coeff_(s,k) = coeff_r[k];
                }
            }

            return result;
        }
    }

/**
 * Class for representing a piecewise polynomial
 *   A function is represented by a polynomial in each section [x_n, x_{n+1}).
 */
    template<typename T, typename Tx>
    class piecewise_polynomial {
    private:
        int k_;

        typedef Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> coefficient_type;

        template<typename TT, typename TXX, typename Op>
        friend piecewise_polynomial<TT,TXX>
        detail::do_op(const piecewise_polynomial<TT,TXX> &f1, const piecewise_polynomial<TT,TXX> &f2, const Op &op);

        template<typename TT, typename TXX>
        friend piecewise_polynomial<TT,TXX>
        operator+(const piecewise_polynomial<TT,TXX> &f1, const piecewise_polynomial<TT,TXX> &f2);

        template<typename TT, typename TXX>
        friend piecewise_polynomial<TT,TXX>
        operator-(const piecewise_polynomial<TT,TXX> &f1, const piecewise_polynomial<TT,TXX> &f2);

        template<typename TT, typename TXX>
        friend const piecewise_polynomial<TT,TXX> operator*(TT scalar, const piecewise_polynomial<TT,TXX> &pp);

        template<typename TT, typename TXX>
        friend
        class piecewise_polynomial;

        /// number of sections
        int n_sections_;

        /// edges of sections. The first and last elements should be -1 and 1, respectively.
        std::vector<Tx> section_edges_;

        /// expansion coefficients [s,l]
        /// The polynomial is represented as
        ///   \sum_{l=0}^k a_{s,l} (x - x_s)^l,
        /// where x_s is the left end point of the s-th section.
        coefficient_type coeff_;

        bool valid_;

        void check_range(Tx x) const {
            if (x < section_edges_[0] || x > section_edges_[section_edges_.size() - 1]) {
                throw std::runtime_error("Give x is out of the range.");
            }
        }

        void check_validity() const {
            assert(section_edges_.size() > 0);
            if (!valid_) {
                throw std::runtime_error("pieacewise_polynomial object is not properly constructed!");
            }
        }

        void set_validity() {
            valid_ = true;
            valid_ = valid_ && (n_sections_ >= 1);
            assert(valid_);
            valid_ = valid_ && (section_edges_.size() == n_sections_ + 1);
            assert(valid_);
            valid_ = valid_ && (coeff_.rows() == n_sections_);
            assert(valid_);
            valid_ = valid_ && (coeff_.cols() == k_ + 1);
            assert(valid_);
            for (int i = 0; i < n_sections_; ++i) {
                valid_ = valid_ && (section_edges_[i] < section_edges_[i + 1]);
            }
            assert(valid_);
        }

    public:
        piecewise_polynomial() : k_(-1), n_sections_(0), valid_(false) {};

        /// Construct an object set to zero
        piecewise_polynomial(int k, const std::vector<Tx> &section_edges) : k_(k),
                                                                                n_sections_(section_edges.size() - 1),
                                                                                section_edges_(section_edges),
                                                                                coeff_(n_sections_, k + 1),
                                                                                valid_(false) {
            coeff_.setZero();
            set_validity();
            check_validity();//this may throw
        };

        piecewise_polynomial(int n_section,
                             const std::vector<Tx> &section_edges,
                             const Eigen::Matrix<T, Eigen::Dynamic,Eigen::Dynamic> &coeff) : k_(coeff.cols() - 1),
                                                                      n_sections_(section_edges.size() - 1),
                                                                      section_edges_(section_edges),
                                                                      coeff_(coeff), valid_(false) {
            assert(n_section == section_edges.size()-1);
            set_validity();
            check_validity();//this may throw
        };

        /// Copy operator
        piecewise_polynomial<T,Tx> &operator=(const piecewise_polynomial<T,Tx> &other) {
            k_ = other.k_;
            n_sections_ = other.n_sections_;
            section_edges_ = other.section_edges_;
            coeff_ = other.coeff_;
            valid_ = other.valid_;
            return *this;
        }

        /// Order of the polynomial
        int order() const {
            return k_;
        }

        /// Number of sections
        int num_sections() const {
#ifndef NDEBUG
            check_validity();
#endif
            return n_sections_;
        }

        /// Return an end point. The index i runs from 0 (smallest) to num_sections()+1 (largest).
        inline Tx section_edge(int i) const {
            assert(i >= 0 && i < section_edges_.size());
#ifndef NDEBUG
            check_validity();
#endif
            return section_edges_[i];
        }

        /// Return a refence to end points
        const std::vector<Tx> &section_edges() const {
#ifndef NDEBUG
            check_validity();
#endif
            return section_edges_;
        }

        /// Return the coefficient of $x^p$ for the given section.
        inline const T &coefficient(int i, int p) const {
            assert(i >= 0 && i < section_edges_.size());
            assert(p >= 0 && p <= k_);
#ifndef NDEBUG
            check_validity();
#endif
            return coeff_(i, p);
        }

        /// Return a reference to the coefficient of $x^p$ for the given section.
        inline T &coefficient(int i, int p) {
            assert(i >= 0 && i < section_edges_.size());
            assert(p >= 0 && p <= k_);
#ifndef NDEBUG
            check_validity();
#endif
            return coeff_(i, p);
        }

        /// Set to zero
        void set_zero() {
            coeff_.setZero();
        }

        /// Compute the value at x.
        template<typename Tw = Tx>
        inline T compute_value(Tx x) const {
#ifndef NDEBUG
            check_validity();
#endif
            return compute_value<Tw>(x, find_section(x));
        }

        inline Tx derivative(Tx x, int order, int section = -1) const {
#ifndef NDEBUG
            check_validity();
#endif
            int section_eval = section >= 0 ? section : find_section(x);
            auto dx = x - section_edges_[section_eval];

            std::vector<T> coeff_deriv(k_+1, 0.0);
            for (int p = 0; p < k_+1; ++p) {
                coeff_deriv[p] = coeff_(section_eval, p);
            }

            for (int m=0; m<order; ++m) {
                for (int p = 0; p < k_ ; ++p) {
                    coeff_deriv[p] = (p+1)*coeff_deriv[p+1];
                }
                coeff_deriv[k_] = 0;
            }

            Tx r = 0.0;
            Tx x_pow = 1.0;
            for (int p = 0; p < k_ + 1; ++p) {
                r += coeff_deriv[p] * x_pow;
                x_pow *= dx;
            }
            return r;
        }

        /// Compute the value at x. x must be in the given section.
        template<typename Tw = Tx>
        inline T compute_value(Tx x, int section) const {
#ifndef NDEBUG
            check_validity();
#endif
            assert (x >= section_edges_[section] && x <= section_edges_[section + 1]);

            Tw dx = static_cast<Tw>(x) - static_cast<Tw>(section_edges_[section]);
            Tw r = 0.0, x_pow = 1.0;
            for (int p = 0; p < k_ + 1; ++p) {
                r += static_cast<Tw>(coeff_(section, p)) * x_pow;
                x_pow *= dx;
            }
            return static_cast<T>(r);
        }

        /// Find the section involving the given x
        int find_section(Tx x) const {
#ifndef NDEBUG
            check_validity();
#endif

            if (x == section_edges_[0]) {
                return 0;
            } else if (x == section_edges_.back()) {
                return section_edges_.size() - 2;
            }

            auto it = std::upper_bound(section_edges_.begin(), section_edges_.end(), x);
            --it;
            return (&(*it) - &(section_edges_[0]));
        }

        /// Compute overlap <this | other> with complex conjugate. The two objects must have the same sections.
        template<class T2>
        T overlap(const piecewise_polynomial<T2,Tx> &other) const {
            check_validity();
            if (section_edges_ != other.section_edges_) {
                throw std::runtime_error(
                        "Computing overlap between piecewise polynomials with different section edges are not supported");
            }
            using Tr = typename std::remove_const<decltype(static_cast<T>(1.0) * static_cast<T2>(1.0))>::type;

            const int k = this->order();
            const int k2 = other.order();

            Tr r = 0.0;
            std::vector<double> x_min_power(k + k2 + 2), dx_power(k + k2 + 2);

            for (int s = 0; s < n_sections_; ++s) {
                dx_power[0] = 1.0;
                const double dx = static_cast<double>(section_edges_[s + 1] - section_edges_[s]);
                for (int p = 1; p < dx_power.size(); ++p) {
                    dx_power[p] = dx * dx_power[p - 1];
                }

                for (int p = 0; p < k + 1; ++p) {
                    for (int p2 = 0; p2 < k2 + 1; ++p2) {
                        auto prod =  detail::outer_product((Tr) coeff_(s, p), (Tr) other.coeff_(s, p2));
                        r += prod * dx_power[p + p2 + 1] / (p + p2 + 1.0);
                    }
                }
            }
            return r;
        }

        /// Compute squared norm
        double squared_norm() const {
            return static_cast<double>(this->overlap(*this));
        }

        /// Returns whether or not two objects are numerically the same.
        bool operator==(const piecewise_polynomial<T,Tx> &other) const {
            return (n_sections_ == other.n_sections_) &&
                   (section_edges_ == other.section_edges_) &&
                   (coeff_ == other.coeff_);
        }

    };//class pieacewise_polynomial

#ifdef SWIG
    %template(piecewise_polynomial) piecewise_polynomial<double,mpfr::mpreal>;
#endif

/// Add piecewise_polynomial objects
    template<typename T, typename Tx>
    piecewise_polynomial<T,Tx>
    operator+(const piecewise_polynomial<T,Tx> &f1, const piecewise_polynomial<T,Tx> &f2) {
        return detail::do_op(f1, f2, detail::pp_plus<T>());
    }

/// Substract piecewise_polynomial objects
    template<typename T, typename Tx>
    piecewise_polynomial<T,Tx>
    operator-(const piecewise_polynomial<T,Tx> &f1, const piecewise_polynomial<T,Tx> &f2) {
        return detail::do_op(f1, f2, detail::pp_minus<T>());
    }

/// Multiply piecewise_polynomial by a scalar
    template<typename T, typename Tx>
    const piecewise_polynomial<T,Tx> operator*(T scalar, const piecewise_polynomial<T,Tx> &pp) {
        piecewise_polynomial<T,Tx> pp_copy(pp);
        pp_copy.coeff_ *= scalar;
        return pp_copy;
    }

/// Gram-Schmidt orthonormalization
    template<typename T, typename Tx>
    void orthonormalize(std::vector<piecewise_polynomial<T,Tx> > &pps) {
        typedef piecewise_polynomial<T,Tx> pp_type;

        for (int l = 0; l < pps.size(); ++l) {
            pp_type pp_new(pps[l]);
            for (int l2 = 0; l2 < l; ++l2) {
                const T overlap = pps[l2].overlap(pps[l]);
                pp_new = pp_new - overlap * pps[l2];
            }
            double norm = pp_new.overlap(pp_new);
            pps[l] = (1.0 / std::sqrt(norm)) * pp_new;
        }
    }

/**
 * @brief Class representing a pieacewise polynomial and utilities
 */
    template<typename T, typename Tx>
    piecewise_polynomial<T,Tx>
    multiply(const piecewise_polynomial<T,Tx> &f1, const piecewise_polynomial<T,Tx> &f2) {
        if (f1.section_edges() != f2.section_edges()) {
            throw std::runtime_error("Two pieacewise_polynomial objects with different sections cannot be multiplied.");
        }

        const int k1 = f1.order();
        const int k2 = f2.order();
        const int k = k1 + k2;

        piecewise_polynomial<T,Tx> r(k, f1.section_edges());
        for (int s=0; s < f1.num_sections(); ++s) {
            for (int p = 0; p <= k; p++) {
                r.coefficient(s, p) = 0.0;
            }
            for (int p1 = 0; p1 <= k1; ++p1) {
                for (int p2 = 0; p2 <= k2; ++p2) {
                    r.coefficient(s, p1+p2) += f1.coefficient(s, p1) * f2.coefficient(s, p2);
                }
            }
        }
        return r;
    }

    template<typename T, typename Tx>
    T
    integrate(const piecewise_polynomial<T,Tx> &y) {
        const int k = y.order();

        std::vector<T> rvec(k+1, 0.0);
        for (int s=0; s < y.num_sections(); ++s) {
            auto dx = y.section_edge(s+1) - y.section_edge(s);
            auto dx_power = dx;
            for (int p=0; p<=k; ++p) {
                rvec[p] += dx_power * y.coefficient(s, p);
                dx_power *= dx;
            }
        }

        double r = 0.0;
        for (int p=0; p<=k; ++p) {
            r += rvec[p]/(p+1);
        }
        return r;
    }

    inline std::ostream& operator<<(std::ostream& stream, const piecewise_polynomial<double,mpfr::mpreal>& value) {
        mpfr_prec_t prec = value.section_edge(0).get_prec();

        stream << prec << std::endl;
        stream << value.order() << std::endl;
        stream << value.num_sections() << std::endl;

        for (int i=0; i<value.num_sections()+1; ++i) {
            stream << std::setprecision(mpfr::bits2digits(prec)) << value.section_edge(i) << std::endl;
        }

        for (int s=0; s<value.num_sections(); ++s) {
            for (int i=0; i<value.order()+1; ++i) {
                stream << value.coefficient(s,i) << std::endl;
            }
        }

        return stream;
    }

    inline std::ostream& operator<<(std::ostream& stream, const piecewise_polynomial<mpfr::mpreal,mpfr::mpreal>& value) {
        mpfr_prec_t prec = value.section_edge(0).get_prec();

        if (prec != value.coefficient(0,0).get_prec()) {
            throw std::runtime_error("All mpreal values in a piecewise polynomial must have the same precision.");
        }

        stream << prec << std::endl;
        stream << value.order() << std::endl;
        stream << value.num_sections() << std::endl;

        for (int i=0; i<value.num_sections()+1; ++i) {
            stream << std::setprecision(mpfr::bits2digits(prec)) << value.section_edge(i) << std::endl;
        }

        for (int s=0; s<value.num_sections(); ++s) {
            for (int i=0; i<value.order()+1; ++i) {
                stream << std::setprecision(mpfr::bits2digits(prec)) << value.coefficient(s,i) << std::endl;
            }
        }

        return stream;
    }

    inline std::istream& operator>>(std::istream& stream, piecewise_polynomial<double,mpfr::mpreal>& value) {
        mpfr_prec_t prec;
        int k, ns;

        stream >> prec;
        stream >> k;
        stream >> ns;

        std::vector<mpfr::mpreal> section_edges(ns+1);
        for (int i=0; i<ns+1; ++i) {
            section_edges[i].set_prec(prec);
            stream >> section_edges[i];
        }


        Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> coeff(ns,k+1);
        for (int s=0; s<ns; ++s) {
            for (int i=0; i<k+1; ++i) {
                stream >> coeff(s, i);
            }
        }

        value = piecewise_polynomial<double,mpfr::mpreal>(ns, section_edges, coeff);

        return stream;
    }

    inline std::istream& operator>>(std::istream& stream, piecewise_polynomial<mpfr::mpreal,mpfr::mpreal>& value) {
        mpfr_prec_t prec;
        int k, ns;

        stream >> prec;
        stream >> k;
        stream >> ns;

        std::vector<mpfr::mpreal> section_edges(ns+1);
        for (int i=0; i<ns+1; ++i) {
            section_edges[i].set_prec(prec);
            stream >> section_edges[i];
        }


        Eigen::Matrix<mpfr::mpreal,Eigen::Dynamic,Eigen::Dynamic> coeff(ns,k+1);
        for (int s=0; s<ns; ++s) {
            for (int i=0; i<k+1; ++i) {
                stream >> coeff(s, i);
            }
        }

        value = piecewise_polynomial<mpfr::mpreal,mpfr::mpreal>(ns, section_edges, coeff);

        return stream;
    }

}
