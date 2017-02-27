#include "kernel.hpp"
#include <cmath>

Kernel::Kernel(double h, int N, double mass) {
    _h = h;
    _N = N;
    _mass = mass;
    _fac1 = 4.0 * h * h * h * N;
}

double Kernel::Function(double r) const {
    double v = r / _h;
    if (v >= 0.0 && v < 1.0) {
        v = (4.0 - 6.0 * r * r + 3.0 * r * r * r);
    } else if (v >= 1.0 && v < 2.0) {
        v = pow((2.0 - r), 3.0);
    } else {
        return 0.0;
    }
    return v / _fac1;
}

void Kernel::FOD(double rx, double ry, double rz, double r, double* ret) {
    double v = r / _h;

    if (v >= 0.0 && v < 1.0) {
        ret[0] = -12.0 * rx  + 9.0 * rx * r;
        ret[1] = -12.0 * ry  + 9.0 * ry * r;
        ret[2] = -12.0 * rz  + 9.0 * rz * r;
    } else if (v >= 1.0 && v < 2.0) {
        ret[0] = -3.0 * rx * (4 / r + r - 4);
        ret[1] = -3.0 * ry * (4 / r + r - 4);
        ret[2] = -3.0 * rz * (4 / r + r - 4);
    } else {
        ret[0] = 0.0;
        ret[1] = 0.0;
        ret[2] = 0.0;
        return;
    }

    ret[0] /= _fac1;
    ret[1] /= _fac1;
    ret[2] /= _fac1;
}

double Kernel::InterpolateDensity(double rx, double ry, double rz, double* density, double* position) const {
    double sum = 0.0;
    double distance = 0.0;

    for (int j = 0; j < _N; j++) {
        distance = pow(
            (position[j * 3] - rx) * (position[j * 3] - rx)
                + (position[j * 3 + 1] - ry) * (position[j * 3 + 1] - ry)
                + (position[j * 3 + 2] - rz) * (position[j * 3 + 2] - rz),
            0.5
        );
        sum += _mass * density[j] * this->Function(distance);
    }

    return sum;
}