#include "output/debug_renderer.h"
#include "kernel/cubic_spline.h"
#include "kernel/poly_6.h"
#include "kernel/spiky.h"
#include "kernel/wendland.h"
#include "output/vtk.h"
#include "output/ascii_output.h"
#include "simulation/compute.h"
#include <yaml-cpp/yaml.h>
#include <omp.h>

/// Checks if there has been a SDL_QUIT event since the last time SDL events
/// were checked.
///
/// @param event SDL_Event* A variable where the event (if any) will be stored
/// @return bool If there has been a SDL_QUIT event
bool checkQuitSDLEvent(SDL_Event* event) {
    if (SDL_PollEvent(event)) {
        if (event->type == SDL_QUIT) {
            return true;
        }
    }
    return false;
}

void checkWriteBMPOutput(DebugRenderer& renderer, int timestep) {
    char filename[255];
    sprintf(filename, "output/bmp/%d.bmp", timestep);
    renderer.WriteToBMPFile(std::string(filename));
}

void drawDebugView(DebugRenderer& r, Compute& c, YAML::Node& param) {
    r.ClearScreen();

    // We draw the bounding box as a mesh, but need to load
    // this information only once
    static Mesh box = Mesh();
    static bool isLoaded = false;
    if (!isLoaded) {
        box.loadMeshFromOBJFile(param["bbox_mesh"].as<std::string>());
        box.scaleTo(std::max(
            param["bbox_x_upper"].as<float>() - param["bbox_x_lower"].as<float>(),
            std::max(
                param["bbox_y_upper"].as<float>() - param["bbox_y_lower"].as<float>(),
                param["bbox_z_upper"].as<float>() - param["bbox_z_lower"].as<float>()
            )
        ));
        box.centerOn(Vector3D<float>(
            param["bbox_x_upper"].as<float>() + param["bbox_x_lower"].as<float>(),
            param["bbox_y_upper"].as<float>() + param["bbox_y_lower"].as<float>(),
            param["bbox_z_upper"].as<float>() + param["bbox_z_lower"].as<float>()
        ) * 0.5f);
        isLoaded = true;
        printf("Loaded boundary mesh\n");
    }
    r.DrawWireframe(&box, Color::red);

    // We draw the initialization domain as a mesh, if set, but need to
    // load this information only once
    if (param["domain_type"].as<std::string>() == "mesh") {
        static Mesh domMesh = Mesh();
        static bool domMeshIsLoaded = false;
        if (!domMeshIsLoaded) {
            domMesh.loadMeshFromOBJFile(param["mesh_file"].as<std::string>());
            domMeshIsLoaded = true;
            printf("Loaded initialization mesh\n");
        }
        r.DrawWireframe(&domMesh, Color::green);
    }

    r.DrawPoints(c.GetPosition(), param["N"].as<int>(), 1.f);

    r.Render();
}

int main() {
    YAML::Node param = YAML::LoadFile("default_parameter.yaml");

    int nrOfThreads = param["nr_of_threads"].as<int>();
    if (nrOfThreads > 1) {
        omp_set_num_threads(nrOfThreads);
        printf("Running as omp parallel execution with %d threads. This will fail if the cmake variable PARALLEL_BUILD was set to false.\n", nrOfThreads);
    } else {
        omp_set_num_threads(1);
        printf("Running as serial execution. This will fail if the cmake variable PARALLEL_BUILD was set to true.\n");
    }

    DebugRenderer* renderer = new DebugRenderer();
    renderer->Init(param["r_width"].as<int>(), param["r_height"].as<int>());
    renderer->setCameraPosition(
        param["camera_x"].as<float>(),
        param["camera_y"].as<float>(),
        param["camera_z"].as<float>()
    );

    float psize = param["particle_size"].as<float>();
    float rho0 = param["rho0"].as<float>();
    param["mass"] = 8.f * psize * psize * psize * rho0;
    printf("Calculated mass is %f\n", param["mass"].as<float>());
    printf("Calculated cubic volume is %f\n", 8.f * psize * psize * psize);
    printf("Calculated spheric volume is %f\n", 4.f / 3.f * M_PI * psize * psize * psize);

    Poly6 kernel_d = Poly6(param["h"].as<float>(), param["N"].as<int>(), param["mass"].as<float>());
    Spiky kernel_p = Spiky(param["h"].as<float>(), param["N"].as<int>(), param["mass"].as<float>());
    Wendland kernel_v = Wendland(param["h"].as<float>(), param["N"].as<int>(), param["mass"].as<float>());
    Compute compute = Compute(param, &kernel_d, &kernel_p, &kernel_v);

    VTK vtk = VTK("output/vtk/", &kernel_d, 20);
    ASCIIOutput ascii = ASCIIOutput("output/ascii/");

    bool running = true;
    SDL_Event event;
    float t = 0.0;
    int step = 1;

    drawDebugView(*renderer, compute, param);

    while (t < param["tend"].as<float>() && running) {
        printf("Current timestep %f; ", t);
        compute.Timestep();

        if (param["write_vtk"].as<bool>()) {
            printf("Write VTK output; ");
            vtk.WriteDensity(compute.GetDensity(), compute.GetPosition());
        }

        if (param["write_ascii"].as<bool>()) {
            printf("Write ASCII output; ");
            ascii.WriteParticleStatus(
                compute.GetDensity(),
                compute.GetPosition(),
                compute.GetPressure(),
                param
            );
        }

        drawDebugView(*renderer, compute, param);
        if (param["write_bmp"].as<bool>()) {
            checkWriteBMPOutput(*renderer, step);
        }

        t += param["dt"].as<float>();
        step++;

        printf("\n");

        running = !checkQuitSDLEvent(&event);
    }

    printf("End of simulation\n");

    while (running) {
        SDL_Delay(30);
        running = !checkQuitSDLEvent(&event);
    }

    delete renderer;

    return 0;
}