#include "data/neighbors.h"
#include "util/parallel_bounds.h"
#include <omp.h>
#include <cmath>
#include <iostream>

Neighbors::Neighbors(float h, int N, float* bbox) {
    this->h = h;
    this->N = N;
    this->size_x = std::ceil((bbox[3] - bbox[0]) / h);
    this->size_y = std::ceil((bbox[4] - bbox[1]) / h);
    this->size_z = std::ceil((bbox[5] - bbox[2]) / h);

    this->lx = bbox[0];
    this->ly = bbox[1];
    this->lz = bbox[2];

    this->grid = std::vector<std::vector<int>>();
    this->indices = new int[3 * N];

    for (int i = 0; i < size_x; i++) {
        for (int j = 0; j < size_y; j++) {
            for (int k = 0; k < size_z; k++) {
                this->grid.push_back(std::vector<int>());
            }
        }
    }
}

Neighbors::~Neighbors() {
    delete[] this->indices;
}

void Neighbors::sortParticlesIntoGrid(float* positions, ParallelBounds& pBounds) {
    // clear old data
    // we must use a different bounds instance here because the grid size
    // is different from the number of particles
    ParallelBounds gBounds = ParallelBounds(pBounds.getNrOfThreads(), grid.size());

    #pragma omp parallel
    {
        int threadNum = omp_get_thread_num();

        for (int i = gBounds.lower(threadNum); i < gBounds.upper(threadNum); i++) {
            this->grid.at(i).clear();
        }
    }

    // sort particles into grid
    #pragma omp parallel
    {
        float invh = 1.f / h;
        int threadNum = omp_get_thread_num();

        for (int i = pBounds.lower(threadNum); i < pBounds.upper(threadNum); i++) {
            int idx_x = std::floor((positions[i * 3] - lx) * invh);
            int idx_y = std::floor((positions[i * 3 + 1] - ly) * invh);
            int idx_z = std::floor((positions[i * 3 + 2] - lz) * invh);

            this->indices[i * 3] = idx_x;
            this->indices[i * 3 + 1] = idx_y;
            this->indices[i * 3 + 2] = idx_z;

            this->grid.at(idx_z * size_y * size_x + idx_y * size_x + idx_x).push_back(i);
        }
    }
}

void Neighbors::getNeighbors(int idx, std::vector<int>& list) {
    for (int i = std::max(0, this->indices[idx * 3] - 1); i <= std::min(size_x - 1, this->indices[idx * 3] + 1); i++) {
        for (int j = std::max(0, this->indices[idx * 3 + 1] - 1); j <= std::min(size_y - 1, this->indices[idx * 3 + 1] + 1); j++) {
            for (int k = std::max(0, this->indices[idx * 3 + 2] - 1); k <= std::min(size_z - 1, this->indices[idx * 3 + 2] + 1); k++) {
                for (uint l = 0; l < this->grid.at(k * size_y * size_x + j * size_x + i).size(); l++) {
                    list.push_back(this->grid.at(k * size_y * size_x + j * size_x + i).at(l));
                }
            }
        }
    }
}