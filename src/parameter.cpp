#include <cstdio>  // file methods
#include <cstring> // string
#include <cstdlib> // read/write

#include "parameter.hpp"

using namespace std;

Parameter::Parameter() {
    N = 50;
    h = 0.3;
    R = 400;
    tend = 500.0;
    k = 500.0;
    g = 9.81;
    dt = 0.1;
    rho0 = 1000.0;
    mass = rho0 / N;
    mu = 0.1;
    dampening = 0.9;
    // @todo Force scaling should not be necessary
    FSPressure = 0.00003;
    FSGravity = 0.00001;
    FSViscosity = 0.01;
}

void Parameter::Load(const char *filename) {
    FILE* handle = fopen(filename, "r");
    double inval;
    char name[50];

    while (!feof(handle)) {
        if (!fscanf(handle, "%s = %lf\n", name, &inval)) {
            continue;
        }

        if (strcmp(name, "N") == 0){
            N = (int)inval;
        } else if (strcmp(name, "h") == 0) {
            h = inval;
        } else if (strcmp(name, "R") == 0) {
            R = (int)inval;
        } else if (strcmp(name, "k") == 0) {
            k = inval;
        } else if (strcmp(name, "dt") == 0) {
            dt = inval;
        } else if (strcmp(name, "tend") == 0) {
            tend = inval;
        } else if (strcmp(name, "g") == 0) {
            g = inval;
        } else if (strcmp(name, "rho0") == 0) {
            rho0 = inval;
        } else if (strcmp(name, "mu") == 0) {
            mu = inval;
        } else if (strcmp(name, "dampening") == 0) {
            dampening = inval;
        } else if (strcmp(name, "fspressure") == 0) {
            FSPressure = inval;
        } else if (strcmp(name, "fsgravity") == 0) {
            FSGravity = inval;
        } else if (strcmp(name, "fsviscosity") == 0) {
            FSViscosity = inval;
        } else {
            printf("Unknown parameter %s\n",name);
        }
    }

    fclose(handle);
}