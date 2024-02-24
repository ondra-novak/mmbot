#pragma once
#ifndef SRC_MAIN_INTEGRAL_H_
#define SRC_MAIN_INTEGRAL_H_

#include <concepts>

template<typename X, typename Y>
class NumericIntegralImplT {
public:


    template<typename Fn>
    void generate(Fn &&fn, X a, X b, X m,  int unsigned levels) {
        xvals.clear();
        yvals.clear();;
        std::size_t need_points = (1<<levels)+1;
        xvals.reserve(need_points);
        yvals.reserve(need_points);
        xvals.push_back(a);
        yvals.push_back(0);
        Y v = do_generate(fn, a, b, fn(a), fn(b), levels);
        xvals.push_back(b);
        yvals.push_back(v);
        Y vm = get(m);
        for (auto &y: yvals) {
            y -= vm;
        }
    }

    Y get(X x) const {
        auto iter = std::lower_bound(xvals.begin(), xvals.end(), x);
        std::size_t idx;
        if (iter == xvals.end()) {
            idx = xvals.size() -1;
        } else {
            idx = std::distance(xvals.begin(), iter);
        }
        auto idx_e = idx;
        if (!idx_e) ++idx_e;
        auto idx_b = idx_e - 1;
        Y vb = yvals[idx_b];
        Y ve = yvals[idx_e];
        double f = (x - xvals[idx_b])/(xvals[idx_e]-xvals[idx_b]);
        return vb +  (ve - vb) * f;
    }

protected:

    std::vector<X> xvals;
    std::vector<Y> yvals;


    template<typename Fn>
    Y do_generate(Fn &&fn, X a, X b, Y fa, Y fb, unsigned int levels, double offset = 0) {
        if (levels) {
            double m = (a+b)*0.5;
            double fm = fn(m);
            double sa = do_generate(fn, a, m, fa, fm, levels-1, offset);
            offset += sa;
            xvals.push_back(m);
            yvals.push_back(offset);
            double sb = do_generate(fn, m, b, fm, fb, levels-1, offset);
            return sa+sb;
        } else {
            double w = b - a;
            double pa = w * fa;
            double pb = w * fb;
            return (pa+pb)*0.5;
        }
    }
};

template<typename X, typename Y>
class NumericIntegralT {
public:

    NumericIntegralT() = default;
    NumericIntegralT(std::shared_ptr<NumericIntegralImplT<X,Y> > ptr):ptr(ptr) {}
    Y operator()(X x) const {
        return ptr->get(x);
    }

protected:
    std::shared_ptr<NumericIntegralImplT<X,Y> > ptr;
};


namespace _details {

    template<typename T>
    struct DeduceArg {
        using Type = typename DeduceArg<decltype(&T::operator())>::Type;
    };

    template<typename Y, typename X>
    struct DeduceArg<Y (*)(X)> {
        using Type = X;
        using RetV = Y;
    };

    template<typename _Res, typename _Tp, bool _Nx, typename _Arg>
    struct DeduceArg<_Res (_Tp::*) (_Arg) noexcept(_Nx)> {
        using Type = _Arg;
        using RetV = _Res;
    };
    template<typename _Res, typename _Tp, bool _Nx, typename _Arg>
    struct DeduceArg<_Res (_Tp::*) (_Arg) const noexcept(_Nx)> {
        using Type = _Arg;
        using RetV = _Res;
    };
    template<typename _Res, typename _Tp, bool _Nx, typename _Arg>
    struct DeduceArg<_Res (_Tp::*) (_Arg) & noexcept(_Nx)> {
        using Type = _Arg;
        using RetV = _Res;
    };
    template<typename _Res, typename _Tp, bool _Nx, typename _Arg>
    struct DeduceArg<_Res (_Tp::*) (_Arg) const & noexcept(_Nx)> {
        using Type = _Arg;
        using RetV = _Res;
    };

}

template<typename Fn, typename X = typename _details::DeduceArg<Fn>::Type>
auto numeric_integral(Fn &&fn, X a, X b, X m, unsigned int levels) {
    using RetVal = std::invoke_result_t<Fn, X>;
    auto ptr = std::make_shared<NumericIntegralImplT<X, RetVal> >();
    ptr->generate(fn, a, b, m, levels);
    return NumericIntegralT<X, RetVal>(ptr);
}



#endif /* SRC_MAIN_INTEGRAL_H_ */
