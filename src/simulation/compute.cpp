#include <cmath>
#include <cstdio>
#include "simulation/compute.h"
#include "simulation/initialization.h"
#include "util/misc_math.h"
#include <string>

Compute::Compute(YAML::Node& param, Kernel* kernel_d, Kernel* kernel_p, Kernel* kernel_v) {
    _isFirstStep = true;
    int N = param["N"].as<int>();
    float h = param["h"].as<float>();

    _param = param;
    _kernel_density = kernel_d;
    _kernel_pressure = kernel_p;
    _kernel_viscosity = kernel_v;

    // We don't necessarily create all N particles,
    // so we need to reduce N to the actual number created
    Initialization init = Initialization(_param);
    float* tmp = new float[3 * N];

    int nrCreated = init.InitPosition(tmp);
    if (nrCreated < N) {
        printf("We wanted %d particles but only created %d\n", N, nrCreated);
    }
    _param["N"] = std::to_string(nrCreated);
    N = nrCreated;

    // copy over positions
    _position = new float[3 * N];
    for (int i = 0; i < 3 * N; i++) {
        _position[i] = tmp[i];
    }
    delete[] tmp;

    _dr = new float[3];
    _fod = new float[3];
    _matr1 = new float[9];
    _velocity_halfs = new float[3 * N];
    _velocity = new float[3 * N];
    _force = new float[3 * N];
    _density = new float[N];
    _pressure = new float[N];

    float* tmp2 = new float[6];
    tmp2[0] = param["bbox_x_lower"].as<float>();
    tmp2[1] = param["bbox_y_lower"].as<float>();
    tmp2[2] = param["bbox_z_lower"].as<float>();
    tmp2[3] = param["bbox_x_upper"].as<float>();
    tmp2[4] = param["bbox_y_upper"].as<float>();
    tmp2[5] = param["bbox_z_upper"].as<float>();

    _neighbors = new Neighbors(h, N, tmp2);
    delete[] tmp2;

    init.InitVelocity(_velocity);
    init.InitPressure(_pressure);
    init.InitForce(_force);
    init.InitDensity(_density);
}

Compute::~Compute() {
    delete[] _position;
    delete[] _velocity;
    delete[] _velocity_halfs;
    delete[] _force;
    delete[] _density;
    delete[] _pressure;
    delete[] _dr;
    delete[] _fod;
    delete[] _matr1;
    delete _neighbors;
}
void Compute::CalculateDensity() {
    int N = _param["N"].as<int>();
    float mass = _param["mass"].as<float>();
    float h = _param["h"].as<float>();

    for (int i = 0; i < N; i++) {
        float sum = 0.0;
        float distance = 0.0;

        std::vector<int> candidates = _neighbors->getNeighbors(i);
        for (uint k = 0; k < candidates.size(); k++) {
            int j = candidates.at(k);

            distance = fastSqrt2(
                (_position[i * 3] - _position[j * 3]) * (_position[i * 3] - _position[j * 3])
                    + (_position[i * 3 + 1] - _position[j * 3 + 1]) * (_position[i * 3 + 1] - _position[j * 3 + 1])
                    + (_position[i * 3 + 2] - _position[j * 3 + 2]) * (_position[i * 3 + 2] - _position[j * 3 + 2])
            );
            if (distance <= h) sum += mass * _kernel_density->ValueOf(distance);
        }

        _density[i] = sum;
    }
}

void Compute::Timestep() {
    _neighbors->sortParticlesIntoGrid(_position);

    this->CalculateDensity();
    this->CalculatePressure();
    this->CalculateForces();
    this->VelocityIntegration(_isFirstStep);
    this->PositionIntegration();

    _isFirstStep = false;
}

void Compute::CalculatePressure() {
    float rho0 = _param["rho0"].as<float>(),
        k = _param["k"].as<float>(),
        gamma = _param["gamma"].as<float>(),
        k_mod = k * rho0 / gamma;
    std::string model = _param["pressure_model"].as<std::string>();

    if (model == "P_GAMMA_ELASTIC") {
        for (int i = 0; i < _param["N"].as<int>(); i++) {
            _pressure[i] = (float)(k_mod * (pow(_density[i] / rho0, gamma) - 1.f));
            _pressure[i] = _pressure[i] * (_pressure[i] > 0);
        }
    } else if (model == "P_DIFFERENCE") {
        for (int i = 0; i < _param["N"].as<int>(); i++) {
            _pressure[i] = k * (_density[i] - rho0);
            _pressure[i] = _pressure[i] * (_pressure[i] > 0);
        }
    }
}

void Compute::CalculateForces() {
    float distance = 0.0;
    float tmp = 0.0;
    float dvx = 0.0, dvy = 0.0, dvz = 0.0;
    float kinNrg = 0.0;
    float mass = _param["mass"].as<float>();
    float g = _param["g"].as<float>();
    float epsilon = _param["epsilon"].as<float>();
    float h = _param["h"].as<float>();
    float mu = _param["mu"].as<float>();

    for (int i = 0; i < _param["N"].as<int>(); i++) {
        _force[i * 3] = 0.0;
        _force[i * 3 + 1] = 0.0;
        _force[i * 3 + 2] = 0.0;

        kinNrg += 0.5 * mass * (_velocity[i * 3] * _velocity[i * 3]
            + _velocity[i * 3 + 1] * _velocity[i * 3 + 1]
            + _velocity[i * 3 + 2] * _velocity[i * 3 + 2]);

        // 1-body forces, currently only gravity
        _force[i * 3 + 1] += mass * g;

        // 2-body forces
        // @todo Use symmetry to reduce number of calculations by a factor of 2
        float* pressureForce = new float[3];
        pressureForce[0] = 0.0;
        pressureForce[1] = 0.0;
        pressureForce[2] = 0.0;
        float* viscosityForce = new float[3];
        viscosityForce[0] = 0.0;
        viscosityForce[1] = 0.0;
        viscosityForce[2] = 0.0;

        std::vector<int> candidates = _neighbors->getNeighbors(i);

        for (uint k = 0; k < candidates.size(); k++) {
            int j = candidates.at(k);

            if (j == i) {
                continue;
            }

            // Calculate distance vector
            _dr[0] = _position[i * 3] - _position[j * 3];
            _dr[1] = _position[i * 3 + 1] - _position[j * 3 + 1];
            _dr[2] = _position[i * 3 + 2] - _position[j * 3 + 2];
            distance = pow(_dr[0] * _dr[0] + _dr[1] * _dr[1] + _dr[2] * _dr[2], 0.5);

            // Pressure force
            _kernel_pressure->FOD(_dr[0], _dr[1], _dr[2], distance, _fod);
            tmp = mass * (_pressure[i] / (_density[i] * _density[i])
                + _pressure[j] / (_density[j] * _density[j]));

            pressureForce[0] += tmp * _fod[0];
            pressureForce[1] += tmp * _fod[1];
            pressureForce[2] += tmp * _fod[2];

            // Viscosity force
            _kernel_viscosity->FOD(_dr[0], _dr[1], _dr[2], distance, _fod);
            dvx = _velocity[i * 3] - _velocity[j * 3];
            dvy = _velocity[i * 3 + 1] - _velocity[j * 3 + 1];
            dvz = _velocity[i * 3 + 2] - _velocity[j * 3 + 2];
            tmp = mass / _density[j] / (
                _dr[0] * _dr[0] + _dr[1] * _dr[1] + _dr[2] * _dr[2]
                + epsilon * h * h
            );

            viscosityForce[0] += tmp * dvx * (_dr[0] * _fod[0]);
            viscosityForce[1] += tmp * dvy * (_dr[1] * _fod[1]);
            viscosityForce[2] += tmp * dvz * (_dr[2] * _fod[2]);
        }

        _force[i * 3] -= mass * pressureForce[0];
        _force[i * 3 + 1] -= mass * pressureForce[1];
        _force[i * 3 + 2] -= mass * pressureForce[2];

        _force[i * 3] += mass * mu * 2.f * viscosityForce[0];
        _force[i * 3 + 1] += mass * mu * 2.f * viscosityForce[1];
        _force[i * 3 + 2] += mass * mu * 2.f * viscosityForce[2];

        delete pressureForce;
        delete viscosityForce;
    }

    printf("Kinetic energy: %f;", kinNrg);
}

void Compute::VelocityIntegration(bool firstStep) {
    float inv_mass = 1.f / _param["mass"].as<float>();
    float dt = _param["dt"].as<float>();

    if (firstStep) {
        for (int i = 0; i < _param["N"].as<int>(); i++) {
            _velocity_halfs[i * 3] = _velocity[i * 3] +  0.5 * dt * _force[i * 3] * inv_mass;
            _velocity_halfs[i * 3 + 1] = _velocity[i * 3 + 1] +  0.5 * dt * _force[i * 3 + 1] * inv_mass;
            _velocity_halfs[i * 3 + 2] = _velocity[i * 3 + 2] +  0.5 * dt * _force[i * 3 + 2] * inv_mass;
        }
        return;
    }

    for (int i = 0; i < _param["N"].as<int>(); i++) {
        _velocity_halfs[i * 3] += dt * _force[i * 3] * inv_mass;
        _velocity_halfs[i * 3 + 1] += dt * _force[i * 3 + 1] * inv_mass;
        _velocity_halfs[i * 3 + 2] += dt * _force[i * 3 + 2] * inv_mass;
        _velocity[i * 3] = _velocity_halfs[i * 3] + 0.5 * dt * _force[i * 3] * inv_mass;
        _velocity[i * 3 + 1] = _velocity_halfs[i * 3 + 1] + 0.5 * dt * _force[i * 3 + 1] * inv_mass;
        _velocity[i * 3 + 2] = _velocity_halfs[i * 3 + 2] + 0.5 * dt * _force[i * 3 + 2] * inv_mass;
    }
}

void Compute::PositionIntegration() {
    float dt = _param["dt"].as<float>();
    float lx = _param["bbox_x_lower"].as<float>();
    float ly = _param["bbox_y_lower"].as<float>();
    float lz = _param["bbox_z_lower"].as<float>();
    float ux = _param["bbox_x_upper"].as<float>();
    float uy = _param["bbox_y_upper"].as<float>();
    float uz = _param["bbox_z_upper"].as<float>();

    // Do _position integration (leap frog) with collision detection
    // against the standard cube spanned by (0,0,0)x(1,1,1).
    // On collision we reflect the respective component and rescale the
    // velocity so the new absolute velocity is the old one multiplied
    // with a dampening factor, except for the upper z-bound.
    // @todo We do the reflection with the old velocity, but the path after
    // the reflection should be traversed with the dampened velocity
    // @todo The dampening is uniform now, but should actually dampen the
    // reflected component stronger, while still maintaining the correct
    // direction
    // @todo in fact, the reflection is too crude, since the kernel seemingly
    // extends into the wall, but should be "squished" against it
    float newval = 0.0;
    for (int i = 0; i < _param["N"].as<int>(); i++) {
        bool damp = false;

        // Reflection of x component
        newval = _position[i * 3] + _velocity_halfs[i * 3] * dt;
        if (newval < lx) {
            damp = true;
            _position[i * 3] = lx + fabs(lx - newval);
            _velocity_halfs[i * 3] = -_velocity_halfs[i * 3];
        } else if (newval > ux) {
            damp = true;
            _position[i * 3] = ux - fabs(newval - ux);
            _velocity_halfs[i * 3] = -_velocity_halfs[i * 3];
        } else {
            _position[i * 3] = newval;
        }

        // Reflection of y component
        newval = _position[i * 3 + 1] + _velocity_halfs[i * 3 + 1] * dt;
        if (newval < ly) {
            damp = true;
            _position[i * 3 + 1] = ly + fabs(ly - newval);
            _velocity_halfs[i * 3 + 1] = -_velocity_halfs[i * 3 + 1];
        } else if (newval > uy) {
            damp = true;
            _position[i * 3 + 1] = uy - fabs(newval - uy);
            _velocity_halfs[i * 3 + 1] = -_velocity_halfs[i * 3 + 1];
        } else {
            _position[i * 3 + 1] = newval;
        }

        // Reflection of z component
        newval = _position[i * 3 + 2] + _velocity_halfs[i * 3 + 2] * dt;
        if (newval < lz) {
            damp = true;
            _position[i * 3 + 2] = lz + fabs(lz - newval);
            _velocity_halfs[i * 3 + 2] = -_velocity_halfs[i * 3 + 2];
        } else if (newval > uz) {
            damp = true;
            _position[i * 3 + 2] = uz - fabs(newval - uz);
            _velocity_halfs[i * 3 + 2] = -_velocity_halfs[i * 3 + 2];
        } else {
            _position[i * 3 + 2] = newval;
        }

        // Dampening
        if (damp) {
            _velocity_halfs[i * 3] *= _param["dampening"].as<float>();
            _velocity_halfs[i * 3 + 1] *= _param["dampening"].as<float>();
            _velocity_halfs[i * 3 + 2] *= _param["dampening"].as<float>();
        }
    }
}

float* Compute::GetPosition() {
    return _position;
}

float* Compute::GetDensity() {
    return _density;
}

float* Compute::GetPressure() {
    return _pressure;
}
