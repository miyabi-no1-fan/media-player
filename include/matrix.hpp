#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace aglea {

template <typename T, std::size_t M, std::size_t N>
class mat {
    std::array<std::array<T, N>, M> data;

    // for floating-point
    static constexpr bool USE_APPROX_EQ = true;
    static constexpr std::int64_t MAX_ULPS_DIFF = 1;
    static constexpr T ABSOLUTE_TOLERANCE = 4;

   public:
    /*
        CONSTRUCTORS
    */

    // default is zero-initialized
    constexpr mat() : data() {}

    // conversion
    template <typename Ts, std::size_t Ms, std::size_t Ns>
    explicit constexpr mat(const mat<Ts, Ms, Ns>& o) : mat() {
        for (std::size_t i = 0; i < std::min(M, Ms); i++)
            for (std::size_t j = 0; j < std::min(N, Ns); j++)
                (*this)[i, j] = static_cast<T>(o[i, j]);
    }

    template <typename U, std::size_t... Ns>
        requires(sizeof...(Ns) == M) && ((Ns == N) && ...)
    constexpr mat(const U (&... rows)[Ns]) : mat() {
        if constexpr (M > 0) {
            const U* src[M] = { rows... };
            for (std::size_t i = 0; i < M; i++)
                for (std::size_t j = 0; j < N; j++)
                    (*this)[i, j] = static_cast<T>(src[i][j]);
        }
    }

    template <std::forward_iterator It>
    explicit constexpr mat(It first, It last)
        requires(N == 1)
        : mat() {
        if ((std::size_t)std::ranges::distance(first, last) != M)
            throw std::invalid_argument("mat constructor: construct vector len mismatch");
        for (std::size_t i = 0; i < M; i++)
            (*this)[i, 0] = static_cast<T>(*first++);
    }

    template <std::ranges::forward_range R>
    explicit constexpr mat(const R& r)
        requires(N == 1)
        : mat(std::ranges::begin(r), std::ranges::end(r)) {}

    static constexpr mat identity()
        requires(M == N)
    {
        mat x = zeros();
        for (std::size_t i = 0; i < N; i++)
            x[i, i] = T(1);
        return x;
    }

    static constexpr mat zeros() {
        return mat();
    }

    // clang-format off
    /*
        INDEXING
    */
    constexpr const std::array<T, N>& operator[](std::size_t i) const { assert(i < M); return data[i]; }
    constexpr const T& operator[](std::size_t i, std::size_t j) const { assert(i < M && j < N); return data[i][j]; }

    constexpr std::array<T, N>& operator[](std::size_t i) { assert(i < M); return data[i]; }
    constexpr T& operator[](std::size_t i, std::size_t j) { assert(i < M && j < N); return data[i][j]; }

    /*
        BASIC ARITHMETIC
    */
    constexpr mat operator+(const mat& other) const { return map(other, [](auto&& a, auto&& b) { return a + b; }); }
    constexpr mat operator-(const mat& other) const { return map(other, [](auto&& a, auto&& b) { return a - b; }); }
    constexpr mat operator*(const T& scalar) const { return map([&scalar](auto&& a) { return a * scalar; }); }
    constexpr mat operator/(const T& scalar) const { return map([&scalar](auto&& a) { return a / scalar; }); }
    constexpr mat& operator+=(const mat& other) { return for_each([&other](T& v, std::size_t i, std::size_t j) { v += other[i, j]; }); }
    constexpr mat& operator-=(const mat& other) { return for_each([&other](T& v, std::size_t i, std::size_t j) { v -= other[i, j]; }); }
    constexpr mat& operator*=(const T& scalar) { return for_each([&scalar](T& v, std::size_t, std::size_t) { v *= scalar; }); }
    constexpr mat& operator/=(const T& scalar) { return for_each([&scalar](T& v, std::size_t, std::size_t) { v /= scalar; }); }
    friend constexpr mat operator*(const T& scalar, const mat& self) { return self * scalar; }
    constexpr mat operator-() const { mat v; v.for_each([this](auto&& elm, std::size_t i, std::size_t j) {elm = -(*this)[i, j];}); return v; }
    // clang-format on

    constexpr bool operator==(const mat& other) const {
        for (std::size_t i = 0; i < M; i++)
            for (std::size_t j = 0; j < N; j++)
                if (!this->cmpeq((*this)[i, j], other[i, j])) return false;
        return true;
    }

   private:
    constexpr bool cmpeq(const T& a, const T& b)
        requires(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)
    {
        if (!USE_APPROX_EQ) return a == b;

        // floating-point needs special treatment
        if constexpr (std::floating_point<T>) {
            // clang-format off
            using S =
                std::conditional_t<sizeof(T) == 1, std::int8_t,
                std::conditional_t<sizeof(T) == 2, std::int16_t,
                std::conditional_t<sizeof(T) == 4, std::int32_t,
                std::conditional_t<sizeof(T) == 8, std::int64_t,
                void>>>>;
            // clang-format on

            // https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition
            if (std::signbit(a) != std::signbit(b)) {
                return (a == b);
            }
            using std::abs;
            std::int64_t ulpsDiff = abs(*reinterpret_cast<const S*>(&a) - *reinterpret_cast<const S*>(&b));
            return ulpsDiff <= MAX_ULPS_DIFF;
        } else {
            return a == b;
        }
    }

    constexpr bool is_zero(const T& x) const {
        if (!USE_APPROX_EQ) return x == T{};

        if constexpr (std::floating_point<T>) {
            using std::abs;
            return abs(x) <= std::numeric_limits<T>::epsilon() * T(ABSOLUTE_TOLERANCE);
        } else {
            return x == T{};
        }
    }

   public:
    /*
        LINEAR ALGEBRA CORE
    */

    template <typename R = T>
    constexpr R trace() const
        requires(M == N)
    {
        R res{};
        for (std::size_t i = 0; i < N; i++)
            res += static_cast<R>((*this)[i, i]);
        return res;
    }

    constexpr std::size_t rank() const {
        if constexpr (M == 0 || N == 0) {
            return 0;
        } else {
            auto self = *this;
            auto [_, rank] = gaussian_elimination(self);
            return rank;
        }
    }

    template <std::size_t L>
    constexpr mat<T, M, L> operator*(const mat<T, N, L>& other) const {
        auto c = mat<T, M, L>::zeros();
        for (std::size_t i = 0; i < M; i++)
            for (std::size_t j = 0; j < L; j++)
                for (std::size_t k = 0; k < N; k++)
                    c[i, j] += (*this)[i, k] * other[k, j];
        return c;
    }

    constexpr mat<T, N, M> transpose() const {
        mat<T, N, M> x;
        for (std::size_t i = 0; i < M; i++)
            for (std::size_t j = 0; j < N; j++)
                x[j, i] = (*this)[i, j];
        return x;
    }

    template <typename R = T>
    constexpr R determinant() const
        requires(M == N)
    {
        if constexpr (N == 0) {
            return static_cast<R>(1);
        } else if constexpr (N == 1) {
            return static_cast<R>((*this)[0, 0]);
        } else if constexpr (N == 2) {
            R a = static_cast<R>((*this)[0, 0]);
            R b = static_cast<R>((*this)[0, 1]);
            R c = static_cast<R>((*this)[1, 0]);
            R d = static_cast<R>((*this)[1, 1]);
            return a * d - b * c;
        } else {
            mat res = *this;  // copy

            auto [row_swapped, rank] = gaussian_elimination(res);

            if (rank != N) {
                return R{};
            }

            R det = static_cast<R>(res[0, 0]);
            for (std::size_t i = 1; i < N; i++)
                det *= static_cast<R>(res[i, i]);

            if ((row_swapped & 1) == 0)
                return det;
            else
                return -det;
        }
    }

    /**
    @return if mat is singular mat, return nullopt,
            else return inversed mat.
    */
    constexpr std::optional<mat> inverse() const
        requires(M == N && N > 0)
    {
        auto mat = *this;  // copy
        auto res = identity();
        auto rank = row_reduced_echelon(mat, &res);
        if (rank == N)
            return res;
        else
            return {};
    }

    /**
    @return Row-reduced echelon form of self
    */
    constexpr mat rref() const {
        if constexpr (M == 0 || N == 0) {
            return *this;
        } else {
            auto res = *this;
            row_reduced_echelon(res);
            return res;
        }
    }

    template <typename R = T>
    constexpr R length() const
        requires(N == 1)
    {
        R result{};
        this->for_each([&result](auto& v, std::size_t, std::size_t) {
            R x = static_cast<R>(v);
            x *= x;  // v^2
            result += x;
        });
        return std::sqrt(result);
    }

   private:
    /**
    @brief Transform self into row-reduced echelon form
    @param mirror A mirror such that any modifications applied on self will also apply on the mirror. Ignored if mirror == nullptr.
    @return self's rank
    */
    template <std::size_t L = 0>
    static constexpr std::size_t row_reduced_echelon(mat<T, M, N>& self, mat<T, M, L>* mirror = nullptr)
        requires(std::convertible_to<int, T> && M > 0 && N > 0)
    {
        auto [_, rank] = gaussian_elimination(self, mirror);

        auto pcol = [&](std::size_t i) {
            std::size_t c = i;
            while (c < N && self.is_zero(self[i, c]))
                c++;
            return c;
        };

        for (std::size_t p = 0; p < rank; p++) {
            for (std::size_t i = p + 1; i < rank; i++) {
                std::size_t c = pcol(i);

                auto scalar = self[p, c] / self[i, c];
                self[p, c] = T{};  // = 0
                for (std::size_t j = c + 1; j < N; j++) {
                    self[p, j] -= scalar * self[i, j];
                    if (self.is_zero(self[p, j])) self[p, j] = T{};  // clamp to 0
                }

                if (mirror)
                    for (std::size_t j = 0; j < L; j++) {
                        (*mirror)[p, j] -= scalar * (*mirror)[i, j];
                        if (self.is_zero((*mirror)[p, j])) (*mirror)[p, j] = T{};
                    }
            }

            std::size_t c = pcol(p);
            auto scalar = self[p, c];
            self[p, c] = T(1);  // require from int to T
            for (std::size_t j = c + 1; j < N; j++)
                self[p, j] /= scalar;

            if (mirror)
                for (std::size_t j = 0; j < L; j++)
                    (*mirror)[p, j] /= scalar;
        }

        return rank;
    }

    /**
    @brief Perform guassian elimination on self, convert self into row-echelon form.
    @param mirror A mirror such that any modifications applied on self will also apply on the mirror. Ignored if mirror == nullptr.
    @return Number of row swap performed and the mat's rank
    */
    template <std::size_t L = 0>
    static constexpr std::tuple<std::size_t, std::size_t> gaussian_elimination(mat<T, M, N>& self, mat<T, M, L>* mirror = nullptr)
        requires(M > 0 && N > 0)
    {
        // allow custom type to implement
        // friend constexpr T abs(T x)
        using std::abs;

        std::size_t row_swapped = 0;
        std::size_t pivot_row = 0;

        // partial pivoting
        for (std::size_t col = 0; pivot_row < M && col < N; col++) {
            std::size_t best_row = pivot_row;
            auto best_val = abs(self[pivot_row, col]);
            for (std::size_t i = pivot_row + 1; i < M; i++) {
                auto v = abs(self[i, col]);
                if (v > best_val) {
                    best_val = v;
                    best_row = i;
                }
            }

            if (self.is_zero(best_val)) {
                // singular mat
                for (std::size_t i = pivot_row; i < M; i++)
                    self[i, col] = T{};  // skipped column: clear to 0
                // we treated those values as 0s so set them all to 0 to avoid some magic floating-point error
                continue;
            }

            if (best_row != pivot_row) {
                std::swap(self[pivot_row], self[best_row]);
                row_swapped++;

                if (mirror) std::swap((*mirror)[pivot_row], (*mirror)[best_row]);
            }

            for (std::size_t i = pivot_row + 1; i < M; i++) {
                auto scalar = self[i, col] / self[pivot_row, col];
                self[i, col] = T{};  // = 0
                for (std::size_t j = col + 1; j < N; j++) {
                    self[i, j] -= scalar * self[pivot_row, j];
                    if (self.is_zero(self[i, j])) self[i, j] = T{};  // clamp to 0
                }

                if (mirror)
                    for (std::size_t j = 0; j < L; j++) {
                        (*mirror)[i, j] -= scalar * (*mirror)[pivot_row, j];
                        if (self.is_zero((*mirror)[i, j])) (*mirror)[i, j] = T{};
                    }
            }

            pivot_row++;
        }

        return { row_swapped, pivot_row };
    }

   public:
    /*
        UTILITY
    */

    constexpr bool is_singular() const
        requires(M == N) && (N > 0)
    {
        auto temp = *this;
        auto [_, rank] = gaussian_elimination(temp);
        return rank != std::min(M, N);
    }

    constexpr bool is_symmetric() const
        requires(M == N)
    {
        for (std::size_t i = 0; i < M; i++)
            for (std::size_t j = i + 1; j < N; j++)
                if (!this->cmpeq((*this)[i, j], (*this)[j, i])) return false;
        return true;
    }

    constexpr std::array<T, std::min(M, N)> diagonal() const {
        auto res = std::array<T, std::min(M, N)>();
        for (std::size_t i = 0; i < std::min(M, N); i++)
            res[i] = (*this)[i, i];
        return res;
    }

    template <typename R = T>
    constexpr R mean() const
        requires(M > 0 && N > 0)
    {
        R sum{};
        this->for_each([&sum](auto& v, std::size_t, std::size_t) { sum += static_cast<R>(v); });
        return sum / static_cast<R>(M * N);
    }

    constexpr const T& min() const
        requires(M > 0 && N > 0)
    {
        const T* best = &(*this)[0, 0];
        this->for_each([&best](const T& v, std::size_t, std::size_t) { best = (v < *best) ? &v : best; });
        return *best;
    }

    constexpr const T& max() const
        requires(M > 0 && N > 0)
    {
        const T* best = &(*this)[0, 0];
        this->for_each([&best](const T& v, std::size_t, std::size_t) { best = (v > *best) ? &v : best; });
        return *best;
    }

    template <typename U>
        requires(std::convertible_to<U, T>)
    constexpr mat& fill(const U& v) {
        this->for_each([&v](T& x, std::size_t, std::size_t) { x = T(v); });
        return *this;
    }

    // result[i, j] = f(self[i, j], other[i, j]), for all i, j in [0, min(M, Mo, Mr)), [0, min(N, No, Nr))
    template <typename Tr = T, std::size_t Mr = M, std::size_t Nr = N, typename F, typename To, std::size_t Mo, std::size_t No>
    constexpr mat<Tr, Mr, Nr> map(const mat<To, Mo, No>& other, F&& f) const {
        mat<Tr, Mr, Nr> res;
        for (std::size_t i = 0; i < std::min(std::min(M, Mo), Mr); i++)
            for (std::size_t j = 0; j < std::min(std::min(N, No), Nr); j++)
                res[i, j] = Tr(f((*this)[i, j], other[i, j]));
        return res;
    }

    // result[i, j] = f(self[i, j]), for all i, j in [0, min(M, Mr)), [0, min(N, Nr))
    template <typename Tr = T, std::size_t Mr = M, std::size_t Nr = N, typename F>
    constexpr mat<Tr, Mr, Nr> map(F&& f) const {
        mat<Tr, Mr, Nr> res;
        for (std::size_t i = 0; i < std::min(M, Mr); i++)
            for (std::size_t j = 0; j < std::min(N, Nr); j++)
                res[i, j] = Tr(f((*this)[i, j]));
        return res;
    }

    // f(self[i, j], i, j), for all i, j in [0, M), [0, N)
    template <typename F>
    constexpr auto& for_each(this auto& self, F&& f) {
        for (std::size_t i = 0; i < M; i++)
            for (std::size_t j = 0; j < N; j++)
                (void)f(self[i, j], i, j);
        return self;
    }

    // reinterpret_cast this as a flat array
    constexpr const std::array<T, M * N>& as_flat_array() const {
        return *reinterpret_cast<const std::array<T, M * N>*>(this);
    }

    // // for floating-point, approx_equal will be used, you can enable it with this, the default is enable
    // constexpr void enable_approx_equal(this mat& self) { self.USE_APPROX_EQ = true; }
    // // for floating-point, approx_equal will be used, you can disable it with this, the default is enable
    // constexpr void disable_approx_equal(this mat& self) { self.USE_APPROX_EQ = true; }

    // // cursed stuff for floating-point, default is 1
    // // only used when comparing between 2 floating-point value
    // // see https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition
    // constexpr void set_max_ulps_diff(this mat& self, std::int64_t v) { self.MAX_ULPS_DIFF = v; }

    // // cursed stuff for floating-point, default is 4,
    // // absolute tolerance is use when comparing a number against zero
    // // also any result from a floaing-point subtract operations are truncate to zero if it's considered equal to zero through this method
    // // This is ignored if approx equal is disabled, see disable_approx_equal
    // constexpr void set_absolute_tolerance(this mat& self, const T& v) { self.ABSOLUTE_TOLERANCE = v; }
};

template <typename T, std::size_t M>
class vec {
    mat<T, M, 1> data;

   public:
    constexpr vec() : data() {}

    constexpr vec(const mat<T, M, 1>& mat) : data(mat) {}

    constexpr T& operator[](std::size_t i) { return data[i, 0]; }
    constexpr const T& operator[](std::size_t i) const { return data[i, 0]; }

    template <typename... U>
        requires(sizeof...(U) == M)
    constexpr vec(const U&... arg) : vec() {
        std::size_t i = 0;
        (((*this)[i++] = static_cast<T>(arg)), ...);
    }

    // clang-format off
    constexpr vec operator+(const vec& o) const { return vec(data + o.data); }
    constexpr vec operator-(const vec& o) const { return vec(data - o.data); }
    constexpr vec operator*(const T& scalar) const { return vec(data * scalar); }
    constexpr vec operator/(const T& scalar) const { return vec(data / scalar); }
    constexpr vec& operator+=(const vec& o) { data += o.data; return *this; }
    constexpr vec& operator-=(const vec& o) { data -= o.data; return *this; }
    constexpr vec& operator*=(const T& scalar) { data *= scalar; return *this; }
    constexpr vec& operator/=(const T& scalar) { data /= scalar; return *this; }
    friend constexpr vec operator*(const T& scalar, const vec& self) { return vec(self * scalar); }
    // clang-format on
    constexpr vec operator-() const {
        vec v;
        v.data = -data;
        return v;
    }

    constexpr vec operator*(const vec& o) const { return vec(data * o.data); }
    constexpr vec& operator*=(const vec& o) {
        data = data * o.data;
        return *this;
    }

    template <typename R = T>
    constexpr R length() const { return data.template length<R>(); }

    template <typename R = T>
    constexpr R sum() const {
        R sum{};
        for (std::size_t i = 0; i < M; i++)
            sum += static_cast<R>((*this)[i]);
        return sum;
    }

    template <typename R = T>
    constexpr R dot(const vec& o) const {
        R dot{};
        for (std::size_t i = 0; i < M; i++)
            dot += static_cast<R>((*this)[i]) * static_cast<R>(o[i]);
        return dot;
    }

    template <typename R = T>
    constexpr R cross(const vec& o) const
        requires(M == 2)
    {
        mat<R, M, 2> x = {
            { static_cast<R>((*this)[0]), static_cast<R>((*this)[1]) },
            { static_cast<R>(o[0]), static_cast<R>(o[1]) },
        };
        return x.determinant();
    }

    template <typename R = T>
    constexpr vec<R, M> cross(const vec& o) const
        requires(M == 3)
    {
        mat<R, 2, 2> i = {
            { static_cast<R>((*this)[1]), static_cast<R>((*this)[2]) },
            { static_cast<R>(o[1]), static_cast<R>(o[2]) },
        };

        mat<R, 2, 2> j = {
            { static_cast<R>((*this)[0]), static_cast<R>((*this)[2]) },
            { static_cast<R>(o[0]), static_cast<R>(o[2]) },
        };

        mat<R, 2, 2> k = {
            { static_cast<R>((*this)[0]), static_cast<R>((*this)[1]) },
            { static_cast<R>(o[0]), static_cast<R>(o[1]) },
        };

        return { i.determinant(), -j.determinant(), k.determinant() };
    }
};

using vec2 = vec<float, 2>;
using vec3 = vec<float, 3>;
using vec4 = vec<float, 4>;

using uvec2 = vec<std::uint32_t, 2>;
using uvec3 = vec<std::uint32_t, 3>;
using uvec4 = vec<std::uint32_t, 4>;

using mat2x2 = mat<float, 2, 2>;
using mat3x3 = mat<float, 3, 3>;
using mat4x4 = mat<float, 4, 4>;

}  // namespace aglea
