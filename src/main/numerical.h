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
	auto oneiter = [&](double v) {
		double j = dfn(v);
		if (j == 0) {
			v = v*1.0001;
			j = dfn(v);
			if (j == 0) {
				v = v*1.0001;
				j = dfn(v);
				if (j == 0) {
					v = v*1.0001;
					j = dfn(v);
				}
			}
		}
		double i = fn(v);
		double r = (i+ofs)/j;
		return v - r;
	};

	double v0 = oneiter(initg);
	double v1 = oneiter(v0);
	while (std::isfinite(v1) && std::fabs(v1-v0)/v0 > accuracy) {
		v0 = v1;
		v1 = oneiter(v0);
	}
	return v1;
}


//adaptive integration - generate table from a, to b, recursive
template<typename Fn, typename Out>
static double generateIntTable2(Fn &&fn, double a, double b, double fa, double fb, double error, double y, Out &&out) {
	double w = b - a;
	double pa = w * fa;
	double pb = w * fb;
	double e = std::abs(pa-pb);
	if (e>error) {
		double m = (a+b)*0.5;
		double fm = fn(m);
		double sa = generateIntTable2(std::forward<Fn>(fn), a, m, fa, fm, error, y, std::forward<Out>(out));
		y+= sa;
		out(m, y);
		double sb = generateIntTable2(std::forward<Fn>(fn), m, b, fm, fb, error, y, std::forward<Out>(out));
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
	double r = generateIntTable2(std::forward<Fn>(fn), a, b, fa, fb, error, y, std::forward<Out>(out));
	out(b, y+r);
}



#endif /* SRC_MAIN_NUMERICAL_H_ */
