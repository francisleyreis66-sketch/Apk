#version 450
// Filtros de tela feitos na placa de vídeo:
// 0 pixels puros · 1 nítido e liso · 2 suave · 3 linhas de LCD · 4 TV de tubo (CRT)
layout(push_constant) uniform PC {
  vec4 rect;
  vec2 outSize;
  vec2 texSize;
  int mode;
  int rot;
  vec2 extra;
} pc;
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

vec3 nearestTexel(vec2 uv) {
  ivec2 p = ivec2(clamp(floor(uv * pc.texSize), vec2(0.0), pc.texSize - 1.0));
  return texelFetch(tex, p, 0).rgb;
}

// "nítido e liso": cada pixel do jogo vira um bloco do mesmo tamanho; só a borda é suavizada
vec3 sharpBilinear(vec2 uv) {
  vec2 texel = uv * pc.texSize;
  vec2 scale = max(floor(pc.outSize / pc.texSize), vec2(1.0));
  vec2 base = floor(texel);
  vec2 f = fract(texel) - 0.5;
  vec2 range = 0.5 - 0.5 / scale;
  vec2 g = (f - clamp(f, -range, range)) * scale + 0.5;
  return texture(tex, (base + g) / pc.texSize).rgb;
}

void main() {
  vec2 uv = vUV;
  vec3 col;
  if (pc.mode == 0) {
    col = nearestTexel(uv);
  } else if (pc.mode == 2) {
    col = texture(tex, uv).rgb;
  } else if (pc.mode == 3) {
    col = sharpBilinear(uv);
    vec2 f = fract(uv * pc.texSize);
    // como o filtro antigo: metade de cima de cada linha um pouco mais escura e uma coluna fina
    float aa = max(pc.outSize.y / pc.texSize.y, 1.0);  // largura da transição = 1 pixel da tela
    float rowDark = smoothstep(0.5 - 0.5 / aa, 0.5 + 0.5 / aa, f.y);
    float colDark = smoothstep(0.333 - 0.5 / aa, 0.333 + 0.5 / aa, f.x);
    col *= mix(0.78, 1.0, rowDark) * mix(0.86, 1.0, colDark);
  } else if (pc.mode == 4) {
    // TV de tubo: leve curvatura, linhas de varredura com brilho e máscara de fósforo
    vec2 cc = uv * 2.0 - 1.0;
    cc *= 1.0 + 0.035 * vec2(cc.y * cc.y, cc.x * cc.x);
    vec2 cuv = cc * 0.5 + 0.5;
    if (cuv.x < 0.0 || cuv.x > 1.0 || cuv.y < 0.0 || cuv.y > 1.0) { outColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec2 texel = cuv * pc.texSize;
    float y0 = floor(texel.y - 0.5) + 0.5;
    float dy = texel.y - y0;
    vec2 px = vec2(1.0 / pc.texSize.x, 0.0);
    vec3 a = texture(tex, vec2(texel.x, y0) / pc.texSize).rgb;
    vec3 b = texture(tex, vec2(texel.x, y0 + 1.0) / pc.texSize).rgb;
    a = mix(a, 0.5 * (texture(tex, vec2(texel.x, y0) / pc.texSize - px).rgb + texture(tex, vec2(texel.x, y0) / pc.texSize + px).rgb), 0.25);
    b = mix(b, 0.5 * (texture(tex, vec2(texel.x, y0 + 1.0) / pc.texSize - px).rgb + texture(tex, vec2(texel.x, y0 + 1.0) / pc.texSize + px).rgb), 0.25);
    // a largura da linha depende do brilho (pontos claros "incham" como no tubo)
    vec3 wa = exp(-pow(dy / (0.30 + 0.20 * a), vec3(2.0)));
    vec3 wb = exp(-pow((1.0 - dy) / (0.30 + 0.20 * b), vec3(2.0)));
    col = a * wa + b * wb;
    float m = mod(gl_FragCoord.x, 3.0);
    vec3 mask = m < 1.0 ? vec3(1.0, 0.82, 0.82) : m < 2.0 ? vec3(0.82, 1.0, 0.82) : vec3(0.82, 0.82, 1.0);
    col *= mask * 1.35;
    vec2 v = cuv * (1.0 - cuv);
    col *= clamp(pow(v.x * v.y * 16.0, 0.12), 0.0, 1.0);  // cantos um pouco mais escuros
  } else {
    col = sharpBilinear(uv);
  }
  outColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
