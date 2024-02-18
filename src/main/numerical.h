/*
 * numerical.h
 *
 *  Created on: 16. 6. 2020
 *      Author: ondra
 */

#ifndef SRC_MAIN_NUMERICAL_H_
#define SRC_MAIN_NUMERICAL_H_

#include <cmath>
#include <cstdlib>
#include <type_traits>

namespace {

const double accuracy = 1e-6;

}


template<typename Fn>
double numeric_search_r1(double middle, Fn &&fn) {
	double min = 0;
	double max = middle;
	double ref = fn(middle);
	if (ref == 0 || std::isnan(ref)) return middle;
	double md = (min+max)/2;
	int cnt = 1000;
	while ((max - min) / md > accuracy && --cnt) {
		double v = fn(md);
		if (std::isnan(v)) break;
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return md;
		md = (min+max)/2;
	}
	return md;

}

template<typename Fn>
double numeric_search_r2(double middle, Fn &&fn) {
	double min = 0;
	double max = 1.0/middle;
	double ref = fn(middle);
	if (ref == 0|| std::isnan(ref)) return middle;
	double md = (min+max)/2;
	int cnt = 1000;
	while (md * (1.0 / min - 1.0 / max) > accuracy && --cnt) {
		double v = fn(1.0/md);
		if (std::isnan(v)) break;
		double ml = v * ref;
		if (ml > 0) max = md;
		else if (ml < 0) min = md;
		else return 1.0/md;
		md = (min+max)/2;
	}
	return 1.0/md;

}

///Calculate quadrature of given function in given range
/**
 * calculates ∫ fn(x) dx  in range (a,b)
 * @param fn function to calculate
 * @param a range from
 * @param b range to
 * @param steps number of steps. Function takes 3 values for each step + one extra step at the beginning.
 * @return value of quadrature
 *
 * @note Uses Simpson rule 3/8
 */

template<typename Fn>
double numeric_integral(Fn &&fn, double a, double b, unsigned int steps=33) {
	if (a == b) return 0;
	double fna = fn(a);
	double res = 0;
	double ia = a;
	for (unsigned int i = 0; i < steps; i++) {
		double ib = a+(b-a)*(i+1)/steps;
		double fnb = fn(ib);
		double fnc = fn((2*ia+ib)/3.0);
		double fnd = fn((ia+2*ib)/3.0);
		double r = (ib - ia)*(fna+3*fnc+3*fnd+fnb)/8.0;
		ia = ib;
		fna = fnb;
		res += r;
	}
	return res;

}

template<typename Fn, typename dFn>
double newtonRoot(Fn &&fn, dFn &&dfn, double ofs, double initg) {
	double x = initg;
	double y = fn(x)-ofs;
	while (std::abs(y)/(std::abs(ofs)+std::abs(y))>accuracy) {
		double dy = dfn(x);
		x = x - y/dy;
		y = fn(x)-ofs;
	}
	return x;
}


//adaptive integration - generate table from a, to b, recursive
template<typename Fn, typename Out>
static double generateIntTable2(Fn &&fn, double a, double b, double fa, double fb, double error, double y,int lev, Out &&out) {
	double w = b - a;
	double pa = w * fa;
	double pb = w * fb;
	double e = std::abs(pa-pb);
	if (e>error && lev < 16) {
		double m = (a+b)*0.5;
		double fm = fn(m);
		double sa = generateIntTable2(std::forward<Fn>(fn), a, m, fa, fm, error, y, lev+1, std::forward<Out>(out));
		y+= sa;
		out(m, y);
		double sb = generateIntTable2(std::forward<Fn>(fn), m, b, fm, fb, error, y, lev+1, std::forward<Out>(out));
		return sa+sb;
	} else {
		return (pa+pb)*0.5;
	}
}

//adaptive integration - generate table from a, to b, recursive
/**
 *
 * @param fn function to integrate
 * @param a from
 * @param b to
 * @param error maximal error
 * @param y initial value (can be zero)
 * @param out output function (x,y)
 */
template<typename Fn, typename Out>
static void generateIntTable(Fn &&fn, double a, double b, double error, double y, Out &&out) {
	out(a, y);
	double fa=fn(a);
	double fb=fn(b);
	double r = generateIntTable2(std::forward<Fn>(fn), a, b, fa, fb, error, y, 0,std::forward<Out>(out));
	out(b, y+r);
}

#if 0

class IntegralTable {
public:
    template<typename Fn>
    IntegralTable(Fn &&fn, double a, double b, double error, double y) {
        generateIntTable([&](double x){
            return baseFn(x);
        }, a, b, error, y, [&](double x, double y) {
            itable.push_back({x,y});
        });
    }

    double operator()(double x) const {
        double r;
        auto iter = std::lower_bound(itable.begin(), itable.end(), Point(x,0), sortPoints);
        Point p1,p2;
        if (iter == itable.begin()) {
            p1 = *iter;
            p2 = *(iter+1);
        } else if (iter == itable.end()) {
            p1 = itable[itable.size()-2];
            p2 = itable[itable.size()-1];
        } else {
            p1 = *(iter-1);
            p2 = *(iter);
        }
        double f = (x - p1.first)/(p2.first - p1.first);
        r = p1.second + (p2.second - p1.second) * f;
        return r;
    }

    double get_transformed(double sx, double sy, double offs_x, double offs_y, double x) {
        //sy - scale y - just a constant
        //sx - scale x = f(sx * x) - replace sx * x with substitution u)
                // f(u) -> sx * x = u -> sx = du
        //offs_x - just decrease x by off_x
        //offs_y -
    }


protected:
    std::vector<std::pair<double, double> > itable;

    static bool sortPoints(const Point &a, const Point &b) {
        return a.first < b.first;
    }


};

#endif



template<unsigned int iterations = 30>
class Numerics {
public:

    ///find root of an function between two intervals
    /**
     * @param from first interval. Point on this interval must be defined or infinity
     * @param to second interval. Point on this interval don't need to be defined
     * @param fn function
     * @return returns found root or NaN if not found
     */

    template<typename Fn>
    static constexpr double find_root(double from, double to, Fn &&fn) {
        double rv = fn(from);
        //test whether we are close to root on from - in this case, return the from directly
        if (rv < 2*std::numeric_limits<double>::min() && rv > 2-std::numeric_limits<double>::min()) return from;
        bool found = false;
        for (unsigned int i = 0; i < iterations; ++i) {
            double mid = (from + to)*0.5;
            double v = fn(mid);
            if (v * rv < 0) {
                found = true;
                to = mid;
            } else {
                from = mid;
            }
        }
        return found?(from + to)*0.5:std::numeric_limits<double>::signaling_NaN();
    }

    ///find root of an function between specified point and +infinity
    /**
     * tries to find root between given point and infinity on positive side of axis
     *
     * @param from where start to search, must be positive and must be defined or infinity
     * @param fn function to search
     * @return found root or NaN
     */
    template<typename Fn>
    static constexpr double find_root_to_inf(double from, Fn &&fn) {
        auto trnfn = [&](double x) {return fn(1.0/x);};
        double res = find_root(1.0/from, 0.0, trnfn);
        if (res == std::numeric_limits<double>::signaling_NaN()) return res;
        else return 1.0/res;
    }

    ///find root of an function between specified point and zero
    /**
     * tries to find root between given point and zero on positive side of axis
     *
     * @param from where start to search, must be positive and must be defined or infinity
     * @param fn function to search
     * @return found root or NaN
     */
    template<typename Fn>
    static constexpr double find_root_to_zero(double from, Fn &&fn) {
        return find_root(from, 0.0, fn);
    }


    ///find root in given direction
    /**
     * @param from starting point
     * @param dir if direction is positive, it searches for root towards infinite. If dir is negative,
     * it searches for root towards zero. If dir is zero it returns NaN
     * @param fn function
     * @return
     */
    template<typename Fn>
    static constexpr double find_root_pos(double from, double dir, Fn &&fn) {
        if (dir > 0) {
            return find_root_to_inf(from, fn);
        } else if (dir < 0) {
            return find_root_to_zero(from, fn);
        } else return std::numeric_limits<double>::signaling_NaN();
    }
};

static constexpr bool isFinite(double x) {
    return (-std::numeric_limits<double>::infinity() < x && x < std::numeric_limits<double>::infinity());
}

static constexpr double not_nan(double x, double val = 0.0) {
    return isFinite(x)?x:val;
}


#endif /* SRC_MAIN_NUMERICAL_H_ */
