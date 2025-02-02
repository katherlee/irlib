#pragma once

#include <algorithm>

#include <Eigen/Core>
#include <Eigen/CXX11/Tensor>

#include "../piecewise_polynomial.hpp"
#include "spline.hpp"

namespace irlib {
    //template<typename T>
    //reutrn
    template<typename T> T const_pi();

    template<>
    inline mpfr::mpreal const_pi<mpfr::mpreal>() {
        return mpfr::const_pi();
    }

    template<>
    inline double const_pi<double>() {
        return M_PI;
    }

    inline std::complex<mpreal> my_exp(const mpreal& z_img) {
        return std::complex<mpreal>(
                mpfr::cos(z_img), mpfr::sin(z_img)
        );
    }

    template<typename mp_type>
    std::vector<std::pair<mp_type, mp_type> >
    composite_gauss_legendre_nodes(
            const std::vector<mp_type> &section_edges,
            const std::vector<std::pair<mp_type, mp_type> > &nodes
    ) {
        int num_sec = section_edges.size() - 1;
        int num_local_nodes = nodes.size();

        std::vector<std::pair<mp_type, mp_type> > all_nodes(num_sec * num_local_nodes);
        for (int s = 0; s < num_sec; ++s) {
            auto a = section_edges[s];
            auto b = section_edges[s + 1];
            for (int n = 0; n < num_local_nodes; ++n) {
                mp_type x = a + ((b - a) / mp_type(2)) * (nodes[n].first + mp_type(1));
                mp_type w = ((b - a) / mp_type(2)) * nodes[n].second;
                all_nodes[s * num_local_nodes + n] = std::make_pair(x, w);
            }
        }
        return all_nodes;
    };

    template<typename Tx, typename Ty, typename F>
    Ty integrate_gauss_legendre(const std::vector<Tx>& section_edges, const F& f, int num_local_nodes) {
        std::vector<std::pair<Tx, Tx>> nodes = detail::gauss_legendre_nodes<Tx>(num_local_nodes);
        auto nodes_x = composite_gauss_legendre_nodes(section_edges, nodes);
        Ty r = 0;
        for (int n=0; n<nodes_x.size(); ++n) {
            r += f(static_cast<Ty>(nodes_x[n].first)) * static_cast<Ty>(nodes_x[n].second);
        }
        return r;
    };

    /**
     * Compute Matrix representation of a given Kernel
     * @tparam Scalar
     * @tparam K
     * @param kernel
     * @param section_edges_x
     * @param section_edges_y
     * @param num_local_nodes
     * @param num_local_poly
     * @return Matrix representation
     */
    template<typename Scalar, typename K>
    Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>
    matrix_rep(const K &kernel,
               const std::vector<mpreal> &section_edges_x,
               const std::vector<mpreal> &section_edges_y,
               int num_local_nodes,
               int num_local_poly) {

        using mpreal_matrix_type = Eigen::Matrix<mpreal, Eigen::Dynamic, Eigen::Dynamic>;
        using matrix_type = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>;

        int num_sec_x = section_edges_x.size() - 1;
        int num_sec_y = section_edges_y.size() - 1;

        // nodes for Gauss-Legendre integration
        std::vector<std::pair<mpreal, mpreal >> nodes = detail::gauss_legendre_nodes<mpreal>(num_local_nodes);
        auto nodes_x = composite_gauss_legendre_nodes(section_edges_x, nodes);
        auto nodes_y = composite_gauss_legendre_nodes(section_edges_y, nodes);

        std::vector<mpreal_matrix_type> phi_x(num_sec_x);
        for (int s = 0; s < num_sec_x; ++s) {
            phi_x[s] = mpreal_matrix_type(num_local_poly, num_local_nodes);
            for (int n = 0; n < num_local_nodes; ++n) {
                for (int l = 0; l < num_local_poly; ++l) {
                    auto leg_val = normalized_legendre_p(l, nodes[n].first);
                    phi_x[s](l, n) = detail::sqrt<mpreal>(mpreal(2) / (section_edges_x[s + 1] - section_edges_x[s])) * leg_val *
                                     nodes_x[s * num_local_nodes + n].second;
                }
            }
        }

        std::vector<mpreal_matrix_type> phi_y(num_sec_y);
        for (int s = 0; s < num_sec_y; ++s) {
            phi_y[s] = mpreal_matrix_type(num_local_poly, num_local_nodes);
            for (int n = 0; n < num_local_nodes; ++n) {
                for (int l = 0; l < num_local_poly; ++l) {
                    auto leg_val = normalized_legendre_p(l, nodes[n].first);
                    phi_y[s](l, n) = detail::sqrt<mpreal>(mpreal("2") / (section_edges_y[s + 1] - section_edges_y[s])) * leg_val *
                                     nodes_y[s * num_local_nodes + n].second;
                }
            }
        }

        matrix_type K_mat(num_sec_x * num_local_poly, num_sec_y * num_local_poly);
        for (int s2 = 0; s2 < num_sec_y; ++s2) {
            for (int s = 0; s < num_sec_x; ++s) {

                mpreal_matrix_type K_nn(num_local_nodes, num_local_nodes);
                for (int n = 0; n < num_local_nodes; ++n) {
                    for (int n2 = 0; n2 < num_local_nodes; ++n2) {
                        K_nn(n, n2) = kernel(static_cast<Scalar>(nodes_x[s * num_local_nodes + n].first),
                                             static_cast<Scalar>(nodes_y[s2 * num_local_nodes + n2].first)
                        );
                    }
                }

                // phi_x(l, n) * K_nn(n, n2) * phi_y(l2, n2)^T
                mpreal_matrix_type r = phi_x[s] * K_nn * phi_y[s2].transpose();

                for (int l2 = 0; l2 < num_local_poly; ++l2) {
                    for (int l = 0; l < num_local_poly; ++l) {
                        K_mat(num_local_poly * s + l, num_local_poly * s2 + l2) = static_cast<Scalar>(r(l, l2));
                    }
                }
            }
        }

        return K_mat;
    }

    /**
     * Estimate absolute errors in ulx and vly by computing the residual of the integral equation
     *   r(x) = u(x) - s^{-1} int_0^1 K(x,y) v(y) dy
     *   This returns an estimate of max_x |r(x)|
     * @tparam K
     * @param ux defined on [0, 1]
     * @param vy defined on [0, 1]
     * @param kernel a function of (x, y)
     * @return residual for ux
     */
    template<typename T, typename K>
    double estimate_residual(const piecewise_polynomial<T,T>& ux, const piecewise_polynomial<T,T>& vy, T s, const K& kernel, int num_local_nodes) {
        auto section_edges_x = ux.section_edges();
        auto section_edges_y = vy.section_edges();

        auto local_nodes = detail::gauss_legendre_nodes<mpfr::mpreal>(num_local_nodes);
        auto nodes_y = composite_gauss_legendre_nodes(section_edges_y, local_nodes);

        std::vector<T> sampling_points(section_edges_x);
        for (int i=0; i < section_edges_x.size()-1; ++i) {
            auto dx = section_edges_x[i+1]-section_edges_x[i];
            sampling_points.push_back(0.25*dx + section_edges_x[i]);
            sampling_points.push_back(0.50*dx + section_edges_x[i]);
            sampling_points.push_back(0.75*dx + section_edges_x[i]);
        }

        // Now we compute residual for u_l(x)
        double residual_x = 0.0;
        for (auto i = 0; i < section_edges_x.size()-1; ++i) {
            auto x = (section_edges_x[i+1] + section_edges_x[i])/2;
            mpfr::mpreal sum(0);
            for (int n=0; n < nodes_y.size(); ++n) {
                auto y = nodes_y[n].first;
                auto w = nodes_y[n].second;
                sum += w * kernel(x,y) * vy.compute_value(y);
            }
            auto diff = mpfr::abs(sum/s - ux.compute_value(x));
            residual_x = std::max(residual_x, static_cast<double>(diff));
        }

        return residual_x;
    }

    template<typename SVD>
    void check_SVD(const MatrixXmp& Kmat_even, const SVD& svd_even) {
        MatrixXmp S(svd_even.matrixU().cols(), svd_even.matrixV().cols());
        MatrixXmp invS(svd_even.matrixU().cols(), svd_even.matrixV().cols());
        S.setZero();
        invS.setZero();
        for (int l=0; l<svd_even.singularValues().rows(); ++l) {
            S(l,l) = svd_even.singularValues()[l];
            invS(l,l) = 1/svd_even.singularValues()[l];
        }
        for (int l=0; l<svd_even.matrixV().cols(); ++l) {
            MatrixXmp diff = (Kmat_even * svd_even.matrixV().col(l))/svd_even.singularValues()[l] - svd_even.matrixU().col(l);
            std::cout << "Residual of SVD at l = " << l << " " << diff.squaredNorm() << " " << svd_even.singularValues()[l] << std::endl;
        }
        /*
         * MatrixXmp diff = Kmat_even - svd_even.matrixU() * S * svd_even.matrixV().transpose();
        for (int l=0; l<std::min(diff.rows(), diff.cols()); ++l) {
            std::cout << "Residual of SVD at l = " << l << " " << diff(l,l) << std::endl;
        }
         */
    }


    /**
     *
     * @tparam ScalarType
     * @tparam KernelType
     * @r_int_eq absolute errors in ulx and vly estimated by the residual of integral equations. This estimate may be too big
     *    for very small singular values because the residual contains the inverse of singular values.
     */
    template<typename ScalarType, typename KernelType>
    std::tuple<
            std::vector<mpreal>,
            std::vector<piecewise_polynomial<mpreal,mpreal>>,
            std::vector<piecewise_polynomial<mpreal,mpreal>>
    >
    generate_ir_basis_functions_impl(
            const KernelType &kernel,
            int max_dim,
            double sv_cutoff,
            int num_local_poly,
            int num_nodes_gauss_legendre,
            const std::vector<mpreal> &section_edges_x,
            const std::vector<mpreal> &section_edges_y,
            std::vector<double> &residual_x,
            std::vector<double> &residual_y,
            std::pair<double,double>& r_int_eq,
            bool verbose
    ) throw(std::runtime_error) {
        using vector_t = Eigen::Matrix<ScalarType, Eigen::Dynamic, 1>;
        using matrix_t = Eigen::Matrix<ScalarType, Eigen::Dynamic, Eigen::Dynamic>;
        using mp_vector_t = Eigen::Matrix<mpreal, Eigen::Dynamic, 1>;
        using mp_matrix_t = Eigen::Matrix<mpreal, Eigen::Dynamic, Eigen::Dynamic>;

        if (num_local_poly < 2) {
            throw std::runtime_error("num_local_poly < 2! : " + std::to_string(num_local_poly));
        }

        // Compute Kernel matrix and do SVD for even/odd sector
        if (verbose) {
            std::cout << "  Constructing kernel matrix for even sector ... "  << std::flush;
        }
        auto kernel_even = [&](const mpreal &x, const mpreal &y) { return kernel(x, y) + kernel(x, -y); };
        auto Kmat_even = irlib::matrix_rep<ScalarType>(
                kernel_even, section_edges_x, section_edges_y, num_nodes_gauss_legendre, num_local_poly
        );
        if (verbose) {
            std::cout << " done " << std::endl;
            std::cout << "  SVD kernel matrix for even sector ... " << std::flush;
        }
        Eigen::BDCSVD<matrix_t> svd_even(Kmat_even, Eigen::ComputeThinU | Eigen::ComputeThinV);
        if (verbose) {
            std::cout << " done " << std::endl;
            std::cout << "  Constructing kernel matrix for odd sector ... " << std::flush;
        }
        auto kernel_odd = [&](const mpreal &x, const mpreal &y) { return kernel(x, y) - kernel(x, -y); };
        auto Kmat_odd = irlib::matrix_rep<ScalarType>(
                kernel_odd, section_edges_x, section_edges_y, num_nodes_gauss_legendre, num_local_poly
        );
        if (verbose) {
            std::cout << " done " << std::endl;
            std::cout << "  SVD kernel matrix for odd sector ... " << std::flush;
        }
        Eigen::BDCSVD<matrix_t> svd_odd(Kmat_odd, Eigen::ComputeThinU | Eigen::ComputeThinV);
        if (verbose) {
            std::cout << " done " << std::endl;
        }

        // Pick up singular values and basis functions larger than cutoff
        std::vector<mpfr::mpreal> sv;
        std::vector<vector_t> Uvec, Vvec;
        auto s0 = svd_even.singularValues()[0];
        for (int i = 0; i < svd_even.singularValues().size(); ++i) {
            if (sv.size() == max_dim || svd_even.singularValues()[i] / s0 < sv_cutoff) {
                break;
            }
            sv.push_back(svd_even.singularValues()[i]);
            Uvec.push_back(svd_even.matrixU().col(i));
            Vvec.push_back(svd_even.matrixV().col(i));
            if (sv.size() == max_dim || svd_odd.singularValues()[i] / s0 < sv_cutoff) {
                break;
            }
            sv.push_back(svd_odd.singularValues()[i]);
            Uvec.push_back(svd_odd.matrixU().col(i));
            Vvec.push_back(svd_odd.matrixV().col(i));
        }
        assert(sv.size() <= max_dim);

        // Check if singular values are in decreasing order
        for (int l = 0; l < sv.size() - 1; ++l) {
            if (sv[l] < sv[l + 1]) {
                throw std::runtime_error(
                        "Singular values are not in decreasing order. This may be due to numerical round erros. You may ask for fewer basis functions!");
            }
        }

        // Construct basis functions
        std::vector<std::vector<mpreal>> deriv_xm1 = normalized_legendre_p_derivatives(num_local_poly, mpreal("-1"));
        std::vector<mpreal> inv_factorial;
        inv_factorial.push_back(mpreal(1));
        for (int l = 1; l < num_local_poly; ++l) {
            inv_factorial.push_back(inv_factorial.back() / mpreal(l));
        }

        auto gen_pp = [&](const std::vector<mpreal> &section_edges, const std::vector<vector_t> &vectors) {
            std::vector<piecewise_polynomial<mpreal,mpreal>> pp;

            int ns_pp = section_edges.size() - 1;
            for (int v = 0; v < vectors.size(); ++v) {
                Eigen::Matrix<mpreal,Eigen::Dynamic,Eigen::Dynamic> coeff(ns_pp, num_local_poly);
                coeff.setZero();
                int parity = v % 2 == 0 ? 1 : -1;
                // loop over sections in [0, 1]
                for (int s = 0; s < section_edges.size() - 1; ++s) {
                    // loop over normalized Legendre polynomials
                    for (int l = 0; l < num_local_poly; ++l) {
                        mpreal coeff2 = mpreal(1)/mpfr::sqrt(section_edges[s + 1] - section_edges[s]);
                        // loop over the orders of derivatives
                        for (int d = 0; d < num_local_poly; ++d) {
                            mpreal tmp = inv_factorial[d] * coeff2 * vectors[v][s * num_local_poly + l] * deriv_xm1[l][d];
                            coeff(s, d) += tmp;
                            coeff2 *= mpreal(2)/(section_edges[s + 1] - section_edges[s]);
                        }
                    }
                }
                pp.push_back(piecewise_polynomial<mpreal,mpreal>(section_edges.size() - 1, section_edges, coeff));
            }

            return pp;
        };

        auto u_basis_pp = gen_pp(section_edges_x, Uvec);
        auto v_basis_pp = gen_pp(section_edges_y, Vvec);

        for (int i = 0; i < u_basis_pp.size(); ++i) {
            if (u_basis_pp[i].compute_value(1) < 0) {
                u_basis_pp[i] = mpreal(-1.0) * u_basis_pp[i];
                v_basis_pp[i] = mpreal(-1.0) * v_basis_pp[i];
            }
        }

        if (u_basis_pp.size()%2 == 1) {
            r_int_eq.first = estimate_residual(u_basis_pp.back(), v_basis_pp.back(), sv.back(), kernel_even, num_nodes_gauss_legendre);
            auto k_yx = [&](mpreal y, mpreal x) {return kernel_even(x,y);};
            r_int_eq.second = estimate_residual(v_basis_pp.back(), u_basis_pp.back(), sv.back(), k_yx, num_nodes_gauss_legendre);
        } else {
            r_int_eq.first = estimate_residual(u_basis_pp.back(), v_basis_pp.back(), sv.back(), kernel_odd, num_nodes_gauss_legendre);
            auto k_yx = [&](mpreal y, mpreal x) {return kernel_odd(x,y);};
            r_int_eq.second = estimate_residual(v_basis_pp.back(), u_basis_pp.back(), sv.back(), k_yx, num_nodes_gauss_legendre);
        }


        residual_x.resize(section_edges_x.size() - 1);
        residual_y.resize(section_edges_y.size() - 1);
        //std::fill(residual_x.begin(), residual_x.end(), 0.0);
        ////std::fill(residual_y.begin(), residual_y.end(), 0.0);

        {
            auto l = Uvec.size() -1;
            for (int s = 0; s < residual_x.size(); ++s) {
                double dx = static_cast<double>(section_edges_x[s+1]-section_edges_x[s]);
                double a_diff = static_cast<double>(Uvec[l](s * num_local_poly + num_local_poly - 1)) * std::sqrt((2.*l+1)/dx);
                residual_x[s] = std::abs(a_diff);
            }
            for (int s = 0; s < residual_y.size(); ++s) {
                double dy = static_cast<double>(section_edges_y[s+1]-section_edges_y[s]);
                double a_diff = static_cast<double>(Vvec[l](s * num_local_poly + num_local_poly - 1)) * std::sqrt((2.*l+1)/dy);
                residual_y[s] = std::abs(a_diff);
            }
        }

        return std::make_tuple(sv, u_basis_pp, v_basis_pp);
    }

    template<typename ScalarType, typename KernelType>
    std::tuple<
            std::vector<mpreal>,
            std::vector<piecewise_polynomial<mpreal,mpreal>>,
            std::vector<piecewise_polynomial<mpreal,mpreal>>
    >
    generate_ir_basis_functions(
            const KernelType &kernel,
            int max_dim,
            double sv_cutoff = 1e-12,
            bool verbose = false,
            double r_tol = 1e-6,
            int num_local_poly = 10,
            int num_nodes_gauss_legendre = 24
    ) throw(std::runtime_error) {
        using vector_t = Eigen::Matrix<ScalarType, Eigen::Dynamic, 1>;
        // Compute approximate positions of nodes of the highest basis function in the even sector
        std::vector<double> nodes_x, nodes_y;
        if (verbose){
            std::cout << "Computing approximate positions of zeros... ";
            std::tie(nodes_x, nodes_y) = compute_approximate_nodes_even_sector(kernel, 500, std::max(1e-12, sv_cutoff));
            std::cout << "Done" << std::endl;
        }

        auto gen_section_edges = [](const std::vector<double> &nodes) {
            std::vector<mpreal> section_edges;
            section_edges.push_back(0);
            for (int i = 0; i < nodes.size(); ++i) {
                section_edges.push_back(static_cast<mpreal>(nodes[i]));
            }
            section_edges.push_back(1);
            return section_edges;
        };

        auto u = [](const std::vector<mpreal> &section_edges,
                    std::vector<double> &residual, double eps) {
            std::vector<mpreal> section_edges_new(section_edges);
            for (int s = 0; s < section_edges.size() - 1; ++s) {
                if (residual[s] > eps) {
                    section_edges_new.push_back(
                            (section_edges[s] + section_edges[s + 1]) / 2
                    );
                }
            }
            std::sort(section_edges_new.begin(), section_edges_new.end());
            return section_edges_new;
        };

        std::vector<mpreal> section_edges_x = gen_section_edges(nodes_x);
        std::vector<mpreal> section_edges_y = gen_section_edges(nodes_y);

        int ite = 0;

        // Sections are split recursively until convergence is reached.
        while (true) {
            if (verbose) {
                std::cout << "Iteration " << ite+1 << " : " << section_edges_x.size()-1 << " sections for x, " << section_edges_y.size()-1 << " sections for y." << std::endl;
            }
            std::vector<double> residual_x, residual_y;
            std::pair<double,double> r_int_eq;
            auto r = generate_ir_basis_functions_impl<ScalarType>(kernel, max_dim, sv_cutoff, num_local_poly, num_nodes_gauss_legendre,
                    section_edges_x,
                    section_edges_y,
                    residual_x,
                    residual_y,
                    r_int_eq,
                    verbose
            );
            int ns = section_edges_x.size() + section_edges_y.size();

            int dim = std::get<1>(r).size();

            auto a_tol_x = r_tol * std::abs(
                    static_cast<double>(std::get<1>(r).back().compute_value(1))
            );
            auto a_tol_y = r_tol * std::max(
                    std::abs(static_cast<double>(std::get<2>(r)[2*(dim/2)-1].compute_value(1))),
                    std::abs(static_cast<double>(std::get<2>(r)[2*(dim/2)-1].compute_value(0)))
            );

            section_edges_x = u(section_edges_x, residual_x, a_tol_x);
            section_edges_y = u(section_edges_y, residual_y, a_tol_y);

            if (verbose) {
                std::cout << "Iteration " << ite+1 << " : found " << std::get<1>(r).size() <<  " basis functions. " << std::endl;
                std::cout << "Iteration " << ite+1 << " : max_x |u_l(x) - s_l^{-1} dy int_{-1}^1 K(x,y) v_l(y)| = " << r_int_eq.first << " for largest l." << std::endl;
                std::cout << "Iteration " << ite+1 << " : max_y |v_l(y) - s_l^{-1} dx int_{-1}^1 K(x,y) u_l(x)| = " << r_int_eq.second << " for largest l." << std::endl;
                std::cout << "Iteration " << ite+1 << " : residual estimated by expansion coefficients for x = " << *std::max_element(residual_x.begin(),residual_x.end()) << std::endl;
                std::cout << "Iteration " << ite+1 << " : residual estimated by expansion coefficients for y = " << *std::max_element(residual_y.begin(),residual_y.end()) << std::endl;
            }

            if (section_edges_x.size() + section_edges_y.size() == ns) {
                return r;
            }

            ite += 1;
        }

    };

    template<typename T>
    inline std::vector<T> linspace(T minval, T maxval, int N, bool include_last_point = true) {
        int end = include_last_point ? N : N-1;
        std::vector<T> r(end);
        for (int i = 0; i < end; ++i) {
            r[i] = i * (maxval - minval) / (N - T(1)) + minval;
        }
        return r;
    }



    template<typename T>
    piecewise_polynomial<T,T> construct_piecewise_polynomial_cspline(
            const std::vector<T> &x_array, const std::vector<T> &y_array) {
        const int n_points = x_array.size();
        const int n_section = n_points - 1;

        Eigen::Matrix<T,Eigen::Dynamic,Eigen::Dynamic> coeff(n_section, 4);

        // Cubic spline interpolation
        tk::spline<T> spline;
        spline.set_points(x_array, y_array);

        // Construct piecewise_polynomial
        for (int s = 0; s < n_section; ++s) {
            for (int p = 0; p < 4; ++p) {
                coeff(s, p) = spline.get_coeff(s, p);
            }
        }
        return piecewise_polynomial<T,T>(n_section, x_array, coeff);
    };

    /**
     * Find all zeros of a piecewise polynomial defined on (0,1)
     * @tparam T
     * @tparam Tx
     * @param p
     * @param delta tolerance
     * @return
     */
    template<typename T, typename Tx>
    std::vector<Tx> find_zeros(
            const piecewise_polynomial<T,Tx>& p,
            Tx delta
    ) {
        int N = 10000;
        Tx de_cutoff = 3.0;

        std::vector<Tx> tx_vec = linspace<Tx>(0.0, de_cutoff, N);
        std::vector<Tx> x_vec(N), zeros;
        for (int i = 0; i < N; ++i) {
            x_vec[i] = tanh(0.5 * const_pi<Tx>() * sinh(tx_vec[i]));
        }

        for (int i = 0; i < N-1; ++i) {
            if (p.compute_value(x_vec[i]) * p.compute_value(x_vec[i+1]) < 0) {
                Tx x_left = x_vec[i];
                T p_left = p.compute_value(x_vec[i]);

                Tx x_right = x_vec[i+1];
                T p_right = p.compute_value(x_vec[i+1]);

                while (x_right-x_left > delta) {
                    Tx x_mid = (x_left+x_right)/2;
                    if (p_left * p.compute_value(x_mid) > 0) {
                        x_left = x_mid;
                    } else {
                        x_right = x_mid;
                    }
                }
                zeros.push_back((x_left+x_right)/2);
            }
        }

        return zeros;
    };

    template<typename T, typename Tx>
    std::vector<piecewise_polynomial<T,Tx> > cspline_approximation(
            const std::vector<piecewise_polynomial<T,Tx> >& basis_vectors,
            double r_tol
    ) {
        std::vector<piecewise_polynomial<T,Tx> > basis_vectors_cspline;


        // Determine sampling points
        std::set<Tx> x_set{0.0, 1.0};
        int largest_even_l = 2*((basis_vectors.size()-1)/2);
        std::vector<Tx> zeros = find_zeros(basis_vectors[largest_even_l], static_cast<Tx>(1e-10));
        std::copy(zeros.begin(), zeros.end(), std::inserter(x_set, x_set.begin()));
        std::vector<Tx> x(x_set.size());
        std::vector<Tx> y(x_set.size());
        while (true) {
            // construct cubic spline interpolation of the highest-l basis function
            int index = 0;
            x.resize(x_set.size());
            y.resize(x_set.size());
            for (auto it=x_set.begin(); it != x_set.end(); ++it) {
                x[index] = *it;
                y[index] = basis_vectors.back().compute_value(*it);
                ++ index;
            }
            auto cspline = construct_piecewise_polynomial_cspline<T>(x, y);

            // check convergence
            for (int i=0; i < x.size()-1; ++i) {
                auto x_mid = (x[i]+x[i+1])/2;
                auto diff = std::abs(static_cast<double>(
                                             (cspline.compute_value(x_mid)-basis_vectors.back().compute_value(x_mid))/
                                                     basis_vectors.back().compute_value(1)
                                     ));
                if (diff > r_tol) {
                    //std::cout << "add " << x_mid << " " << diff << std::endl;
                    x_set.insert(x_mid);
                }
            }

            if (x_set.size() == x.size()) {
                break;
            }
        }

        int Nx = x.size();

        // construct cspline approximation one by one
        for (int l=0; l < basis_vectors.size(); ++l) {
            for (int ix=0; ix<Nx; ++ix) {
                //std::cout << " final x " << std::setprecision(20) << x[ix] << std::endl;
                y[ix] = basis_vectors[l].compute_value(x[ix]);
            }
            basis_vectors_cspline.push_back(construct_piecewise_polynomial_cspline<T>(x, y));
        }

        return basis_vectors_cspline;
    };

    //Compute nodes (zeros) of Legendre polynomials
    inline std::vector<double> compute_legendre_nodes(int l) {
        double eps = 1e-10;
        if (l > 200) {
            throw std::runtime_error("l > 200 in compute_legendre_nodes");
        }

        std::vector<double> nodes;

        auto leg_diff = [](int l, double x) {
            return l * (x * legendre_p(l, x) - legendre_p(l - 1, x)) / (x * x - 1);
        };

        //i-th zero
        for (int i = 0; i < l / 2; i++) {
            //initial guess
            double x = std::cos(M_PI * (i + 1 - 0.25) / (l + 0.5));

            //Newton-Raphson iteration
            while (true) {
                double leg = legendre_p(l, x);
                double x_new = x - 0.1 * leg / leg_diff(l, x);
                if (std::abs(x_new - x) < eps && std::abs(leg) < eps) {
                    break;
                }
                x = x_new;
            }

            nodes.push_back(x);
            nodes.push_back(-x);
        }

        if (l % 2 == 1) {
            nodes.push_back(0.0);
        }

        std::sort(nodes.begin(), nodes.end());

        return nodes;
    }

    //AVOID USING BOOST_TYPEOF
    template<class T1, class T2>
    struct result_of_overlap {
        typedef std::complex<double> value;
    };

    template<>
    struct result_of_overlap<double, double> {
        typedef double value;
    };

    /// Construct piecewise polynomials representing exponential functions: exp(i w_i x)
    template<class T, typename Tx>
    void construct_exp_functions_coeff(
            const std::vector<T> &w,
            const std::vector<Tx> &section_edges,
            int k,
            Eigen::Tensor<std::complex<T>, 3> &coeffs) {
        const int N = section_edges.size() - 1;

        std::complex<T> z;
        coeffs = Eigen::Tensor<std::complex<T>,3>(w.size(), N, k + 1);

        std::vector<T> pre_factor(k + 1);
        pre_factor[0] = 1.0;
        for (int j = 1; j < k + 1; ++j) {
            pre_factor[j] = pre_factor[j - 1] / j;
        }

        for (int n = 0; n < w.size(); ++n) {
            auto z = std::complex<T>(0.0, w[n]);
            for (int section = 0; section < N; ++section) {
                auto x = section_edges[section];
                std::complex<T> exp0 = std::exp(z * (x + 1));
                std::complex<T> z_power = T(1);
                for (int j = 0; j < k + 1; ++j) {
                    coeffs(n, section, j) = exp0 * z_power * pre_factor[j];
                    z_power *= z;
                }
            }
        }
    }

/**
 *  Compute \int_x0^x1 dx exp(i w (x+1)) (x-x0)^k
 *      for k=0, 1, ..., K+1. x1 = x0+dx.
**/
    template<typename T>
    void compute_Ik(T x0, T dx, T w, int K, std::vector<std::complex<T> >& Ik) {
        auto x1 = x0 + dx;
        auto iw = std::complex<T>(0.0, w);
        auto exp0 = exp(iw*(x0+1));
        auto exp1 = exp(iw*(x1+1));
        auto exp10 = exp(iw*(x1-x0));
        Ik[0] = (exp1 - exp0)/iw;
        //Ik[0] = (exp10 - 1) * exp0 /iw;

        auto dx_k = dx;
        for (int k=1; k<K+1; ++k) {
            Ik[k] = (dx_k * exp1 - static_cast<T>(k) * Ik[k-1])/iw;
            dx_k *= dx;
        }
    }


/**
 * Compute integral of exponential functions and given piecewise polynomials
 *           \int dx exp(i w_i (x+1)) p_j(x),
 *           where w_i are given real double objects and p_j are piecewise polynomials.
 * @tparam T  scalar type of piecewise polynomials
 * @param w vector of w_i in ascending order
 * @param statis Statistics (fermion or boson)
 * @param p vector of piecewise polynomials.
 * @param results  computed results
 */
    template<typename T, typename Tx>
    void compute_integral_with_exp(
            const std::vector<T> &w,
            const std::vector<piecewise_polynomial<T,Tx> > &pp_func,
            Eigen::Tensor<std::complex<T>, 2> &Tnl
    ) {
        typedef std::complex<T> dcomplex;
        typedef piecewise_polynomial<std::complex<T>,Tx> pp_type;
        typedef Eigen::Matrix<std::complex<T>, Eigen::Dynamic, Eigen::Dynamic> ex_matrix_t;
        typedef Eigen::Tensor<std::complex<T>, 2> tensor_t;

        T pi = const_pi<T>();

        //order of polynomials used for representing exponential functions internally.
        const int k_iw = 16;//for debug
        const int k = pp_func[0].order();
        const int n_section = pp_func[0].num_sections();

        std::vector<std::complex<T> > Ik(k+1);

        for (int l = 0; l < pp_func.size(); ++l) {
            if (k != pp_func[l].order()) {
                throw std::runtime_error(
                        "Error in compute_transformation_matrix_to_matsubara: basis functions must be pieacewise polynomials of the same order");
            }
            if (pp_func[l].section_edge(0) != 0 || pp_func[l].section_edge(n_section) != 1) {
                throw std::runtime_error("Piecewise polynomials must be defined on [0,1]");
            }
        }

        const int n_iw = w.size();
        const int n_max = n_iw - 1;

        for (int i = 0; i < w.size() - 1; ++i) {
            if (w[i] > w[i + 1]) {
                throw std::runtime_error("w must be give in ascending order.");
            }
        }

        //Use Taylor expansion for exp(i w_n tau) for w_n*dx < cutoff*M_PI
        const double cutoff = 0.1;

        Eigen::Tensor<std::complex<T>,3> exp_coeffs(w.size(), n_section, k_iw + 1);
        construct_exp_functions_coeff(w, pp_func[0].section_edges(), k_iw, exp_coeffs);

        ex_matrix_t left_mid_matrix(n_iw, k + 1);
        ex_matrix_t left_matrix(n_iw, k_iw + 1);
        ex_matrix_t mid_matrix(k_iw + 1, k + 1);
        ex_matrix_t right_matrix(k + 1, pp_func.size());
        ex_matrix_t r(n_iw, pp_func.size());
        r.setZero();

        std::vector<T> dx_power(k + k_iw + 2);

        for (int s = 0; s < n_section; ++s) {
            auto x0 = pp_func[0].section_edge(s);
            T dx = static_cast<T>(pp_func[0].section_edge(s + 1) - pp_func[0].section_edge(s));
            left_mid_matrix.setZero();

            dx_power[0] = 1.0;
            for (int p = 1; p < dx_power.size(); ++p) {
                dx_power[p] = dx * dx_power[p - 1];
            }

            //Use Taylor expansion for exp(i w_n tau) for w_n*dx < cutoff*M_PI
            auto w_max_cs = cutoff * pi / dx;
            int n_max_cs = -1;
            for (int i = 0; i < w.size(); ++i) {
                if (w[i] <= w_max_cs) {
                    n_max_cs = i;
                }
            }

            //Use Taylor expansion
            if (n_max_cs >= 0) {
                for (int p = 0; p < k_iw + 1; ++p) {
                    for (int p2 = 0; p2 < k + 1; ++p2) {
                        mid_matrix(p, p2) = dx_power[p + p2 + 1] / (p + p2 + 1.0);
                    }
                }

                for (int n = 0; n < n_max_cs + 1; ++n) {
                    for (int p = 0; p < k_iw + 1; ++p) {
                        left_matrix(n, p) = exp_coeffs(n,s,p);
                    }
                }

                left_mid_matrix.block(0, 0, n_max_cs + 1, k + 1) =
                        left_matrix.block(0, 0, n_max_cs + 1, k_iw + 1) * mid_matrix;
            }

            //Otherwise, compute the overlap exactly
            for (int n = std::max(n_max_cs + 1, 0); n <= n_max; ++n) {
                compute_Ik(x0, dx, w[n], k, Ik);
                for (int i=0; i<k+1; ++i) {
                    left_mid_matrix(n, i) = Ik[i];
                }
            }

            for (int l = 0; l < pp_func.size(); ++l) {
                for (int p2 = 0; p2 < k + 1; ++p2) {
                    right_matrix(p2, l) = static_cast<T>(pp_func[l].coefficient(s, p2));
                }
            }

            r += left_mid_matrix * right_matrix;
        }

        Tnl = tensor_t(n_iw, pp_func.size());
        for (int n = 0; n < n_iw; ++n) {
            for (int l = 0; l < pp_func.size(); ++l) {
                Tnl(n, l) = r(n, l);
            }
        }
    }


    /**
    * Compute a transformation matrix from a give orthogonal basis set to Matsubara freq.
    * @tparam T  scalar type
    * @tparam Tx  scalar type for x in [-1,1]
    * @param n_vec indices of Matsubara frequqneices for which matrix elements will be computed (in strictly ascending order).
    *          The Matsubara basis functions look like exp(i PI * (n[i]+1/2)) for fermions, exp(i PI * n[i]) for bosons.
    * @param bf_src orthogonal basis functions on [-1,1]. They must be piecewise polynomials of the same order. Piecewise polynomial representations on [0,1] must be provided.
    *               Basis functions u_l(x) are assumed to be even or odd for even l and odd l, respectively.
    * @param Tnl  computed transformation matrix, results are cast into double
    */
    template<typename T, typename Tx>
    void compute_transformation_matrix_to_matsubara(
            const std::vector<long> &n_vec,
            irlib::statistics::statistics_type statis,
            const std::vector<piecewise_polynomial<T,Tx> > &bf_src,
            Eigen::Tensor<std::complex<double>, 2> &Tnl
    ) {
        typedef std::complex<double> dcomplex;
        typedef Eigen::Matrix<std::complex<T>, Eigen::Dynamic, Eigen::Dynamic> matrix_t;
        typedef Eigen::Tensor<std::complex<T>, 2> tensor_t;

        int Nl = bf_src.size();
        auto pi = const_pi<T>();

        if (n_vec.size() == 0) {
            return;
        }

        for (int i = 0; i < n_vec.size() - 1; ++i) {
            if (n_vec[i] > n_vec[i + 1]) {
                throw std::runtime_error("n_vec must be in strictly ascending order!");
            }
        }

        if (n_vec[0] < 0) {
            throw std::runtime_error("n_vec cannot be negative!");
        }

        long offset = (statis == statistics::FERMIONIC ? 1 : 0);

        // compute tails
        int sign_s = (statis == statistics::FERMIONIC ? -1 : 1);
        // even number close to (bf_src[0].order()/2
        int num_tail = std::min(2*(bf_src[0].order()/2), 4);
        if (num_tail < 4) {
            throw std::runtime_error("num_tail < 4.");
        }
        Eigen::Matrix<std::complex<T>,Eigen::Dynamic,Eigen::Dynamic> tails(bf_src.size(), num_tail);
        const std::complex<T> zi(0.0, 1.0);
        for (int l=0; l<bf_src.size(); ++l) {
            auto ztmp = zi;
            for (int m=0; m<num_tail; ++m) {
                int sign_lm = (l+m)%2==0 ? 1 : -1;
                tails(l,m) = - sqrt(T(2.0)) * pow(T(2), m) * ztmp * static_cast<T>((sign_s - sign_lm) * bf_src[l].derivative(1, m));
                ztmp *= zi;
            }
        }

        // Determine for which Matsubara frequencies tail is used
        // Store those indices in ovec
        std::vector<int> num_low_freq(Nl);
        double eps = 1e-8;
        for (int l=0; l<Nl; ++l) {
            int m_low = (l+offset-1)%2==0 ?  0 : 1;
            int m_high = (l+offset-1)%2==0 ?  num_tail-2 : num_tail-1;
            auto wn_limit = pow(eps * abs(tails(l,m_low)/tails(l,m_high)), 1.0/(m_low-m_high) );
            auto n_limit = 0.5*(wn_limit/pi-offset);

            num_low_freq[l] = std::count_if(n_vec.begin(), n_vec.end(), [&](long n){return n < n_limit;});
        }
        auto max_num_low_freq = *std::max_element(num_low_freq.begin(), num_low_freq.end());
        auto last = n_vec.begin();
        std::advance(last, max_num_low_freq);
        std::vector<long> ovec;
        std::transform(n_vec.begin(), last, std::back_inserter(ovec), [&](long n){return 2*n+offset;});

        // Compute Tnl
        Eigen::Tensor<std::complex<T>, 2> Tnl_low_freq;
        compute_Tbar_ol(ovec, bf_src, Tnl_low_freq);
        Tnl = Eigen::Tensor<std::complex<double>,2>(n_vec.size(), bf_src.size());
        Tnl.setZero();
        for(int l=0; l<Nl; ++l) {
            for(int i=0; i<max_num_low_freq; ++i) {
                Tnl(i,l) = to_dcomplex(Tnl_low_freq(i,l));
            }
        }

        // Relace with tail
        for (int l=0; l<Nl; ++l) {
            for (int i=num_low_freq[l]; i<n_vec.size(); ++i) {
                double wn = (2*n_vec[i]+offset) * M_PI;
                Tnl(i, l) = 0.0;
                for (int m=0; m<num_tail; ++m) {
                    Tnl(i, l) += to_dcomplex(tails(l,m)/pow(wn, m+1));
                }
            }
        }

    }


    /**
    * Compute a transformation matrix (\bar{T}_{nl}) from a give orthogonal basis set to Matsubara freq.
    * @tparam T  scalar type
    * @tparam Tx  scalar type for x \in [-1, 1]
    * @param n indices of Matsubara frequqneices for which matrix elements will be computed (in strictly ascending order).
    *          The Matsubara basis functions look like exp(i PI * (n[i]/2) * (x+1)).
    * @param bf_src orthogonal basis functions on [-1,1]. They must be piecewise polynomials of the same order. Piecewise polynomial representations on [0,1] must be provided.
    *               Basis functions u_l(x) are assumed to be even or odd for even l and odd l, respectively.
    * @param Tnl  computed transformation matrix
    */
    template<typename T, typename Tx>
    void compute_Tbar_ol(
            const std::vector<long> &o_vec,
            const std::vector<irlib::piecewise_polynomial<T,Tx>> &bf_src,
            Eigen::Tensor<std::complex<T>, 2> &Tbar_ol
    ) {
        typedef std::complex<T> dcomplex;
        typedef Eigen::Matrix<std::complex<T>, Eigen::Dynamic, Eigen::Dynamic> matrix_t;
        typedef Eigen::Tensor<std::complex<T>, 2> tensor_t;

        if (o_vec.size() == 0) {
            return;
        }

        for (int i = 0; i < o_vec.size() - 1; ++i) {
            if (o_vec[i] > o_vec[i + 1]) {
                throw std::runtime_error("o must be given in strictly ascending order!");
            }
        }

        std::vector<T> w;
        std::transform(o_vec.begin(), o_vec.end(), std::back_inserter(w), [](long o) { return 0.5 * M_PI * o; });

        compute_integral_with_exp(w, bf_src, Tbar_ol);

        for (int l=0; l<bf_src.size(); ++l) {
            for (int i=0; i<o_vec.size(); ++i) {
                if ( (l+o_vec[i])%2 == 0) {
                    Tbar_ol(i,l) = 2 * Tbar_ol(i,l).real();
                } else {
                    Tbar_ol(i,l) = std::complex<T>(0.0, 2 * Tbar_ol(i,l).imag());
                }
            }
        }

        std::vector<T> inv_norm(bf_src.size());
        for (int l = 0; l < bf_src.size(); ++l) {
            inv_norm[l] = 1. / sqrt(2*bf_src[l].overlap(bf_src[l]));
        }
        for (int n = 0; n < w.size(); ++n) {
            for (int l = 0; l < bf_src.size(); ++l) {
                Tbar_ol(n, l) *= inv_norm[l] * sqrt(static_cast<T>(0.5));
            }
        }
    }


    /**
     * Find approximate positions of nodes for the even singular vectors with the lowest singular value larger than a cutoff
     * @tparam Kernel kernel type
     * @param knl Kernel object
     * @param N Number of points for discretizing the kernel
     * @param cutoff_singular_values smallest relative singular value
     * @return positions of nodes
     */
    template<typename Kernel>
    std::pair<std::vector<double>,std::vector<double>>
    compute_approximate_nodes_even_sector(const Kernel &knl, int N, double cutoff_singular_values) {
        typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> matrix_t;

        double de_cutoff = 2.5;

        //DE mesh for x
        std::vector<double> tx_vec = linspace<double>(0.0, de_cutoff, N);
        std::vector<double> weight_x(N), x_vec(N);
        for (int i = 0; i < N; ++i) {
            x_vec[i] = std::tanh(0.5 * M_PI * std::sinh(tx_vec[i]));
            //sqrt of the weight of DE formula
            weight_x[i] =
                    std::sqrt(0.5 * M_PI * std::cosh(tx_vec[i])) / std::cosh(0.5 * M_PI * std::sinh(tx_vec[i]));
        }

        //DE mesh for y
        std::vector<double> ty_vec = linspace<double>(-de_cutoff, 0.0, N);
        std::vector<double> y_vec(N), weight_y(N);
        for (int i = 0; i < N; ++i) {
            y_vec[i] = std::tanh(0.5 * M_PI * std::sinh(ty_vec[i])) + 1.0;
            //sqrt of the weight of DE formula
            weight_y[i] =
                    std::sqrt(0.5 * M_PI * std::cosh(ty_vec[i])) / std::cosh(0.5 * M_PI * std::sinh(ty_vec[i]));
        }

        matrix_t K(N, N);
        for (int i = 0; i < N; ++i) {
            for (int j = 0; j < N; ++j) {
                K(i, j) = weight_x[i] * static_cast<double>(knl(x_vec[i], y_vec[j]) + knl(x_vec[i], -y_vec[j])) *
                          weight_y[j];
            }
        }

        //Perform SVD
        Eigen::BDCSVD<matrix_t> svd(K, Eigen::ComputeFullU | Eigen::ComputeFullV);
        const Eigen::VectorXd& svalues = svd.singularValues();
        const matrix_t& U = svd.matrixU();
        const matrix_t& V = svd.matrixV();

        //Count non-zero SV
        int dim = N;
        for (int i = 1; i < N; ++i) {
            if (std::abs(svalues(i) / svalues(0)) < cutoff_singular_values) {
                dim = i;
                break;
            }
        }

        //find nodes
        std::vector<double> nodes_x, nodes_y;
        for (int i = 0; i < N - 1; ++i) {
            if (U(i, dim - 1) * U(i + 1, dim - 1) < 0.0) {
                nodes_x.push_back(0.5 * (x_vec[i] + x_vec[i + 1]));
            }
            if (V(i, dim - 1) * V(i+1, dim - 1) < 0.0) {
                nodes_y.push_back(0.5 * (y_vec[i] + y_vec[i + 1]));
            }
        }

        if (nodes_x.size() != dim - 1 || nodes_y.size() != dim - 1) {
            std::cerr << "The number of nodes for x is " << nodes_x.size() << " , which is different from l " << dim-1 << std::endl;
            std::cerr << "The number of nodes for y is " << nodes_y.size() << " , which is different from l " << dim-1 << std::endl;
            for (auto n : nodes_y) {
                std::cout << n << std::endl;
            }
            throw std::runtime_error("The number of nodes is wrong.");
        }

        return std::make_pair(nodes_x, nodes_y);
    }

/**
 *  Compute \int_{-1}^1 dx exp(i w x) p(x)
**/
/*
    inline std::vector<std::complex<mpfr::mpreal>>
    compute_Tnl_impl(const std::vector<piecewise_polynomial<mpreal,mpreal>>& p,
                                                       std::vector<mpreal>& w_vec,
                                                       int digits) {
        auto prec_bak = mpfr::mpreal::get_default_prec();

        mpfr::mpreal::set_default_prec(mpfr::digits2bits(digits));

        int num_local_nodes = 24;
        auto local_nodes = detail::gauss_legendre_nodes<mpreal>(num_local_nodes);
        std::vector<mpreal> section_edges = p.section_edges();
        auto global_nodes = composite_gauss_legendre_nodes(section_edges, local_nodes);
        auto n_local_nodes = local_nodes.size();

        std::vector<mpreal> values(global_nodes.size());
        for (int n=0; n<global_nodes.(); ++n) {
            values[n] = p.compute_value(global_nodes[n].first);
        }

        auto num_sections = p.num_sections();
        auto n_poly = p.order()+1;
        std::vector<mpreal> deriv_values_left(num_sections*n_poly), deriv_values_left(num_sections*n_poly);
        for (int s=0; s<num_sections; s++) {
            for (int k=0; k<n_poly; ++k) {
                deriv_values_left[k + s*n_poly] = p.derivative(p.section_edge(s), k, s);
                deriv_values_right[k + s*n_poly] = p.derivative(p.section_edge(s+1), k, s);
            }
        }

        std::vector<std::complex<mpreal>> result(w_vec.size());
        for (int i=0; i < w_vec.size(); ++i) {
            result[i] = 0;

            for (int s = 0; s < p.num_sections(); ++s) {
                mpreal x0 = p.section_edge(s);
                mpreal x1 = p.section_edge(s+1);

                if (w * (x1-x0) < 0.1 * const_pi<mpreal>()){
                    std::complex<mpreal> tmp(0);
                    for (int n = 0; n < n_local_nodes; ++n) {
                        tmp += values(s*n_local_nodes + n) * my_exp(w*x_smpl) * global_nodes[s*n_local_nodes + n].second;
                    }
                    result[i] += tmp;
                } else {
                    std::complex<mpreal> Jk(0, 0);
                    std::complex<mpreal> iw(0, w);
                    std::complex<mpreal> exp0 = my_exp(w*x0);
                    std::complex<mpreal> exp_tmp = my_exp(w*(x1-x0));

                    //p contains x^0, x^1, xj^2, ..., x^K (K = p.order())
                    for (int k=p.order(); k >= 0; --k) {
                        mpreal f0 = deriv_values_left[k + s*n_poly];
                        mpreal f1 = deriv_values_right[k + s*n_poly];
                        Jk = ((exp_tmp * f1 - f0) * exp0 - Jk)/iw;
                    }
                    result[i] += Jk;
                }
            }

            if (even) {
                result[i] = mpfr::sqrt(2) * result[i].real() * my_exp(w);
            } else {
                result[i] = mpfr::sqrt(2) * std::complex<mpreal>(0, result[i].imag()) * my_exp(w);
            }
        }

        mpfr::mpreal::set_default_prec(prec_bak);

        return result;
    }
    */


    inline std::complex<mpreal> compute_Tnl_tail(const piecewise_polynomial<mpreal,mpreal>& p,
                                   mpreal w,
                                   bool l_even,
                                   irlib::statistics::statistics_type s,
                                   int num_deriv) {
        int sign_s = (s == irlib::statistics::BOSONIC ? 1 : -1);

        if (w == 0.0) {
            throw std::runtime_error("Error zero frequency");
        }

        std::complex<mpreal> result(0);
        std::complex<mpreal> fact(0, 1/w);
        std::complex<mpreal> coeff(fact);
        int sign_l = l_even ? 1 : -1;
        for (int m = 0; m < num_deriv; ++m) {
            int sign_m = m%2 == 0 ? 1 : -1;
            int sign_lm = sign_l * sign_m;
            result += -static_cast<mpreal>(sign_s) * coeff * static_cast<mpreal>(1 - sign_s * sign_lm) * p.derivative(1, m);
            coeff *= fact;
        }

        return result/mpfr::sqrt(2);
    }

/**
 *  Compute \int_{-1}^1 dx exp(i w x) p(x)
**/
    inline std::complex<mpfr::mpreal> compute_Tnl_impl(const piecewise_polynomial<mpreal,mpreal>& p, bool even,
                                                       irlib::statistics::statistics_type s, mpreal w,
                                                       int digits_A = 30, int digits_B = 30) {
        auto prec_bak = mpfr::mpreal::get_default_prec();

        int num_local_nodes = 24;
        auto local_nodes = detail::gauss_legendre_nodes<mpreal>(num_local_nodes);
        std::vector<mpreal> section_edges = p.section_edges();
        auto global_nodes = composite_gauss_legendre_nodes(section_edges, local_nodes);
        auto n_local_nodes = local_nodes.size();

        std::complex<mpreal> result(0);
        for (int s = 0; s < p.num_sections(); ++s) {
            mpreal x0 = p.section_edge(s);
            mpreal x1 = p.section_edge(s+1);

            if (w * (x1-x0) < 0.1 * const_pi<mpreal>()){
                // using low-frequency formula (Gauss-Legendre quadrature)
                mpfr::mpreal::set_default_prec(mpfr::digits2bits(digits_A));
                std::complex<mpreal> tmp(0);
                for (int n = 0; n < n_local_nodes; ++n) {
                    auto x_smpl = global_nodes[s*n_local_nodes + n].first;
                    tmp += p.compute_value(x_smpl) * my_exp(w*x_smpl) * global_nodes[s*n_local_nodes + n].second;
                }
                result += tmp;
            } else {
                // using low-frequency formula (Gauss-Legendre quadrature)
                mpfr::mpreal::set_default_prec(mpfr::digits2bits(digits_B));
                std::complex<mpreal> Jk(0, 0);
                std::complex<mpreal> iw(0, w);
                std::complex<mpreal> exp0 = my_exp(w*x0);
                std::complex<mpreal> exp_tmp = my_exp(w*(x1-x0));

                //p contains x^0, x^1, xj^2, ..., x^K (K = p.order())
                for (int k=p.order(); k >= 0; --k) {
                    mpreal f0 = p.derivative(x0, k, s);
                    mpreal f1 = p.derivative(x1, k, s);
                    Jk = ((exp_tmp * f1 - f0) * exp0 - Jk)/iw;
                }
                result += Jk;
            }
        }

        if (even) {
            result = mpfr::sqrt(2) * result.real() * my_exp(w);
        } else {
            result = mpfr::sqrt(2) * std::complex<mpreal>(0, result.imag()) * my_exp(w);
        }

        // replace with tail
        if (w != 0.0) {
            int num_deriv = p.order()+1;
            auto tail_full = compute_Tnl_tail(p, w, even, s, num_deriv);
            auto tail_two_less = compute_Tnl_tail(p, w, even, s, num_deriv-2);
            if (std::abs((tail_full-tail_two_less)/tail_full) < 1e-12) {
                result = tail_full;
            }
        }

        mpfr::mpreal::set_default_prec(prec_bak);

        return result;
    }

}
