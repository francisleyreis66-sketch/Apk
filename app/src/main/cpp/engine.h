// Motor do XLSM para PDF, em C++ puro (só depende da zlib).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace xp {

using Bytes = std::vector<uint8_t>;

// Erro com mensagem em português, mostrada direto para o usuário.
struct Erro : std::runtime_error {
  explicit Erro(const std::string& m) : std::runtime_error(m) {}
};

// Converte uma planilha .xlsm/.xlsx (bytes) em PDF.
// fit = ajustar à largura da página; todas = todas as abas visíveis (senão só a primeira).
// paginas recebe o número de páginas geradas.
Bytes converterPlanilha(const uint8_t* dados, size_t tam, const std::string& titulo,
                        bool fit, bool todas, int* paginas);

// Junta vários PDFs em um só, na ordem dada. progresso(k, n) é chamado após cada arquivo
// e pode lançar exceção para cancelar.
struct Progresso {
  virtual ~Progresso() = default;
  virtual void passo(int feitos, int total) = 0;
};
Bytes juntarPdfs(const std::vector<Bytes>& pdfs, int* paginas, Progresso* prog);

}  // namespace xp
