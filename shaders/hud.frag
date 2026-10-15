#version 450
layout(location=0) flat in float kind;
layout(location=0) out vec4 outColor;
void main() {
    // A dark translucent backing keeps the light text readable above geometry.
    outColor = kind > 0.5
        ? vec4(0.005, 0.009, 0.015, 0.78)
        : vec4(0.92, 0.96, 1.0, 1.0);
}
