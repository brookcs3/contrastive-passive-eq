// SPDX-FileCopyrightText: 2026 Cameron Brooks
// SPDX-License-Identifier: GPL-3.0-only
//
// Eigenvalues of a small real nonsymmetric matrix (n <= kMaxN), for the zeros and poles of the passive dividers (Network.hpp), where
// each divider's matrix is a block-diagonal branch realisation plus a rank-one term, at most 8 x 8.
//
// Provenance. This is a C++ translation of the nonsymmetric eigenvalue path of JAMA, class Jama.EigenvalueDecomposition (JAMA 1.0.3,
// https://math.nist.gov/javanumerics/jama/, file Jama/EigenvalueDecomposition.java, methods orthes() and hqr2()). JAMA is "a cooperative
// product of The MathWorks and the National Institute of Standards and Technology (NIST) which has been released to the public domain".
// JAMA's own comments state that these methods are derived from the Algol procedures orthes and hqr2 of Martin and Wilkinson (Handbook
// for Automatic Computation, Vol. II, Linear Algebra) and the corresponding EISPACK Fortran subroutines. The comments below of the form
// "JAMA nnn-mmm" give the line range of EigenvalueDecomposition.java that each block translates, so the two can be compared line by line.
//
// Changes from JAMA, all of them removals or guards:
//  - fixed-size arrays instead of heap arrays (no allocation, safe on the audio thread);
//  - eigenvalues only: the accumulation of the transformations into V (orthes' ortran part, and in hqr2) and hqr2's back-substitution for
//    the eigenvectors are left out; they do not feed the eigenvalues;
//  - the nonsymmetric path is used for every input (JAMA switches to tred2 / tql2 when the matrix is exactly symmetric);
//  - the entries below the subdiagonal are set to zero after the Hessenberg reduction (JAMA leaves them unused; hqr2 reads only the
//    Hessenberg part and the positions it clears itself);
//  - a bounded iteration count: at most 30 * max(10, n) QR sweeps in all (the budget LAPACK's dlahqr uses), after which the solver returns
//    false. JAMA has no limit;
//  - input that is not finite, or n outside 1..kMaxN, returns false; a matrix whose Hessenberg form is all zero returns n zero eigenvalues
//    (JAMA would divide zero by zero there). Neither case occurs in this plugin: every branch has a nonzero decay or resonance.
#pragma once
#include <cmath>
#include <complex>

namespace cpeq {

static constexpr int kMaxN = 12;

struct RealMatrix {
    int n = 0;
    double a[kMaxN][kMaxN];
};

namespace jama {

// Householder reduction to upper Hessenberg form, in place on H[0..n-1][0..n-1]. JAMA 299-360 (orthes, without the ortran part 362-387).
inline void orthes(double (&H)[kMaxN][kMaxN], int n)
{
    double ort[kMaxN];
    for (int i = 0; i < kMaxN; i++) ort[i] = 0.0;
    const int low = 0;
    const int high = n - 1;

    for (int m = low + 1; m <= high - 1; m++) {

        // Scale column.
        double scale = 0.0;
        for (int i = m; i <= high; i++) scale = scale + std::fabs(H[i][m - 1]);
        if (scale != 0.0) {

            // Compute Householder transformation.
            double h = 0.0;
            for (int i = high; i >= m; i--) {
                ort[i] = H[i][m - 1] / scale;
                h += ort[i] * ort[i];
            }
            double g = std::sqrt(h);
            if (ort[m] > 0) g = -g;
            h = h - ort[m] * g;
            ort[m] = ort[m] - g;

            // Apply Householder similarity transformation H = (I - u u'/h) H (I - u u'/h)
            for (int j = m; j < n; j++) {
                double f = 0.0;
                for (int i = high; i >= m; i--) f += ort[i] * H[i][j];
                f = f / h;
                for (int i = m; i <= high; i++) H[i][j] -= f * ort[i];
            }
            for (int i = 0; i <= high; i++) {
                double f = 0.0;
                for (int j = high; j >= m; j--) f += ort[j] * H[i][j];
                f = f / h;
                for (int j = m; j <= high; j++) H[i][j] -= f * ort[j];
            }
            ort[m] = scale * ort[m];
            H[m][m - 1] = scale * g;
        }
    }

    // (not in JAMA) make the storage exactly Hessenberg
    for (int i = 2; i < n; i++)
        for (int j = 0; j < i - 1; j++) H[i][j] = 0.0;
}

// Francis double-shift QR iteration on the upper Hessenberg H[0..nn-1][0..nn-1]; eigenvalue k is d[k] + i e[k]. JAMA 412-692 (hqr2 up to
// the end of its main loop, without the V accumulation 521-527 and 678-688, and without the back-substitution 694-849).
inline bool hqr2Values(double (&H)[kMaxN][kMaxN], int nn, double* d, double* e)
{
    // Initialize
    int n = nn - 1;
    const int low = 0;
    const double eps = std::ldexp(1.0, -52);
    double exshift = 0.0;
    double p = 0, q = 0, r = 0, s = 0, z = 0, w, x, y;

    // Compute matrix norm. (JAMA 429-440 also stores roots isolated by balancing, outside low..high; with no balancing there are none.)
    double norm = 0.0;
    for (int i = 0; i < nn; i++)
        for (int j = (i - 1 > 0 ? i - 1 : 0); j < nn; j++) norm = norm + std::fabs(H[i][j]);

    // (not in JAMA) an all-zero Hessenberg form has only zero eigenvalues
    if (norm == 0.0) {
        for (int i = 0; i < nn; i++) { d[i] = 0.0; e[i] = 0.0; }
        return true;
    }

    // (not in JAMA) iteration budget, as LAPACK dlahqr: 30 * max(10, n) sweeps in all
    int budget = 30 * (nn > 10 ? nn : 10);

    // Outer loop over eigenvalue index
    int iter = 0;
    while (n >= low) {

        // Look for single small sub-diagonal element
        int l = n;
        while (l > low) {
            s = std::fabs(H[l - 1][l - 1]) + std::fabs(H[l][l]);
            if (s == 0.0) s = norm;
            if (std::fabs(H[l][l - 1]) < eps * s) break;
            l--;
        }

        // Check for convergence
        // One root found
        if (l == n) {
            H[n][n] = H[n][n] + exshift;
            d[n] = H[n][n];
            e[n] = 0.0;
            n--;
            iter = 0;

        // Two roots found
        } else if (l == n - 1) {
            w = H[n][n - 1] * H[n - 1][n];
            p = (H[n - 1][n - 1] - H[n][n]) / 2.0;
            q = p * p + w;
            z = std::sqrt(std::fabs(q));
            H[n][n] = H[n][n] + exshift;
            H[n - 1][n - 1] = H[n - 1][n - 1] + exshift;
            x = H[n][n];

            // Real pair
            if (q >= 0) {
                if (p >= 0) z = p + z;
                else        z = p - z;
                d[n - 1] = x + z;
                d[n] = d[n - 1];
                if (z != 0.0) d[n] = x - w / z;
                e[n - 1] = 0.0;
                e[n] = 0.0;
                x = H[n][n - 1];
                s = std::fabs(x) + std::fabs(z);
                p = x / s;
                q = z / s;
                r = std::sqrt(p * p + q * q);
                p = p / r;
                q = q / r;

                // Row modification
                for (int j = n - 1; j < nn; j++) {
                    z = H[n - 1][j];
                    H[n - 1][j] = q * z + p * H[n][j];
                    H[n][j] = q * H[n][j] - p * z;
                }

                // Column modification
                for (int i = 0; i <= n; i++) {
                    z = H[i][n - 1];
                    H[i][n - 1] = q * z + p * H[i][n];
                    H[i][n] = q * H[i][n] - p * z;
                }

            // Complex pair
            } else {
                d[n - 1] = x + p;
                d[n] = x + p;
                e[n - 1] = z;
                e[n] = -z;
            }
            n = n - 2;
            iter = 0;

        // No convergence yet
        } else {

            if (--budget < 0) return false;   // (not in JAMA)

            // Form shift
            x = H[n][n];
            y = 0.0;
            w = 0.0;
            if (l < n) {
                y = H[n - 1][n - 1];
                w = H[n][n - 1] * H[n - 1][n];
            }

            // Wilkinson's original ad hoc shift
            if (iter == 10) {
                exshift += x;
                for (int i = low; i <= n; i++) H[i][i] -= x;
                s = std::fabs(H[n][n - 1]) + std::fabs(H[n - 1][n - 2]);
                x = y = 0.75 * s;
                w = -0.4375 * s * s;
            }

            // MATLAB's new ad hoc shift
            if (iter == 30) {
                s = (y - x) / 2.0;
                s = s * s + w;
                if (s > 0) {
                    s = std::sqrt(s);
                    if (y < x) s = -s;
                    s = x - w / ((y - x) / 2.0 + s);
                    for (int i = low; i <= n; i++) H[i][i] -= s;
                    exshift += s;
                    x = y = w = 0.964;
                }
            }

            iter = iter + 1;

            // Look for two consecutive small sub-diagonal elements
            int m = n - 2;
            while (m >= l) {
                z = H[m][m];
                r = x - z;
                s = y - z;
                p = (r * s - w) / H[m + 1][m] + H[m][m + 1];
                q = H[m + 1][m + 1] - z - r - s;
                r = H[m + 2][m + 1];
                s = std::fabs(p) + std::fabs(q) + std::fabs(r);
                p = p / s;
                q = q / s;
                r = r / s;
                if (m == l) break;
                if (std::fabs(H[m][m - 1]) * (std::fabs(q) + std::fabs(r)) <
                    eps * (std::fabs(p) * (std::fabs(H[m - 1][m - 1]) + std::fabs(z) + std::fabs(H[m + 1][m + 1])))) {
                    break;
                }
                m--;
            }

            for (int i = m + 2; i <= n; i++) {
                H[i][i - 2] = 0.0;
                if (i > m + 2) H[i][i - 3] = 0.0;
            }

            // Double QR step involving rows l:n and columns m:n
            for (int k = m; k <= n - 1; k++) {
                const bool notlast = (k != n - 1);
                if (k != m) {
                    p = H[k][k - 1];
                    q = H[k + 1][k - 1];
                    r = (notlast ? H[k + 2][k - 1] : 0.0);
                    x = std::fabs(p) + std::fabs(q) + std::fabs(r);
                    if (x == 0.0) continue;
                    p = p / x;
                    q = q / x;
                    r = r / x;
                }

                s = std::sqrt(p * p + q * q + r * r);
                if (p < 0) s = -s;
                if (s != 0) {
                    if (k != m) H[k][k - 1] = -s * x;
                    else if (l != m) H[k][k - 1] = -H[k][k - 1];
                    p = p + s;
                    x = p / s;
                    y = q / s;
                    z = r / s;
                    q = q / p;
                    r = r / p;

                    // Row modification
                    for (int j = k; j < nn; j++) {
                        p = H[k][j] + q * H[k + 1][j];
                        if (notlast) {
                            p = p + r * H[k + 2][j];
                            H[k + 2][j] = H[k + 2][j] - p * z;
                        }
                        H[k][j] = H[k][j] - p * x;
                        H[k + 1][j] = H[k + 1][j] - p * y;
                    }

                    // Column modification
                    const int imax = n < k + 3 ? n : k + 3;
                    for (int i = 0; i <= imax; i++) {
                        p = x * H[i][k] + y * H[i][k + 1];
                        if (notlast) {
                            p = p + z * H[i][k + 2];
                            H[i][k + 2] = H[i][k + 2] - p * r;
                        }
                        H[i][k] = H[i][k] - p;
                        H[i][k + 1] = H[i][k + 1] - p * q;
                    }
                }  // (s != 0)
            }  // k loop
        }  // check convergence
    }  // while (n >= low)
    return true;
}

} // namespace jama

// Every eigenvalue of M (M.n values into out). M is taken by value, so the caller's matrix is untouched. Returns false if the input is
// not finite, M.n is outside 1..kMaxN, the iteration budget runs out, or a result is not finite.
inline bool eigenvalues(RealMatrix M, std::complex<double>* out)
{
    const int n = M.n;
    if (n < 1 || n > kMaxN) return false;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (!std::isfinite(M.a[i][j])) return false;
    jama::orthes(M.a, n);
    double d[kMaxN], e[kMaxN];
    const bool ok = jama::hqr2Values(M.a, n, d, e);
    bool finite = true;
    for (int i = 0; i < n; i++) {
        if (!ok) { d[i] = 0.0; e[i] = 0.0; }
        finite = finite && std::isfinite(d[i]) && std::isfinite(e[i]);
        out[i] = std::complex<double>(d[i], e[i]);
    }
    return ok && finite;
}

} // namespace cpeq
