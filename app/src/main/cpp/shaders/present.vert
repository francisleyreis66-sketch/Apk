#version 450
// desenha um retângulo (2 triângulos) sem buffer de vértices
layout(push_constant) uniform PC {
  vec4 rect;      // x0, y0, x1, y1 em coordenadas da tela (-1..1, y para baixo)
  vec2 outSize;   // tamanho da imagem na tela, em pixels, na orientação do jogo
  vec2 texSize;   // 256 x 224
  int mode;       // filtro
  int rot;        // 0 normal, 1 girado 90° horário, 2 girado 90° anti-horário
  vec2 extra;
} pc;
layout(location = 0) out vec2 vUV;
void main() {
  vec2 c = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
  gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, c), 0.0, 1.0);
  vec2 uv = c;
  if (pc.rot == 1) uv = vec2(c.y, 1.0 - c.x);
  else if (pc.rot == 2) uv = vec2(1.0 - c.y, c.x);
  vUV = uv;
}
