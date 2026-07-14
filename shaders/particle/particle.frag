#version 450

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    float pointSize;
} pc;

void main() {
    // Circular point sprite
    vec2 uv = gl_PointCoord - vec2(0.5);
    float dist = length(uv);
    if (dist > 0.5) discard;

    // Simple sphere-like shading
    float nz = sqrt(max(0.0, 0.25 - dist * dist));
    vec3 baseColor = vec3(0.76, 0.60, 0.42);  // sandy color
    vec3 shaded = baseColor * (0.4 + 0.6 * nz * 2.0);
    outColor = vec4(shaded, 1.0);
}