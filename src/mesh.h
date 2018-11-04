#pragma once

#include "vector.h"
#include <vector>

struct Vertex {
    Vector pos;
    Vector normal;
    Vector2 uv;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

Mesh load_obj_mesh(const std::string& path);
void compute_normals(const Vector* vertex_positions, uint32_t vertex_count, uint32_t vertex_stride, const uint32_t* indices, uint32_t index_count, Vector* normals);
