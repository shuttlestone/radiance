#version 130

uniform vec2 iResolution;
uniform int iPatternIndex;

void main(void) {
    vec2 uv = gl_FragCoord.xy / iResolution;
    float g = uv.y * 0.5 + 0.1;
    gl_FragColor = vec4(0., 1., 0., float(iPatternIndex) / 8.);
    float w = 2.;
    if(gl_FragCoord.x < w || gl_FragCoord.x > iResolution.x - w || gl_FragCoord.y < w || gl_FragCoord.y > iResolution.y - w) {
        gl_FragColor = vec4(0.9, 0.9, 0.9, 1.);
    }
}