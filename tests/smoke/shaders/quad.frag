#version 450
layout(location = 0) out vec4 o_color;
// Distinct constant color so the readback can be checked exactly.
void main() {
    o_color = vec4(0.25, 0.50, 1.00, 1.0);
}
