package com.xlsmpdf.app;

/** Ponte para o motor em C++ (libxlsmpdf.so). */
public final class Motor {
    static {
        System.loadLibrary("xlsmpdf");
    }

    private Motor() {}

    /** Recebe o progresso da junção; devolve false para cancelar. */
    public interface Progresso {
        boolean passo(int feitos, int total);
    }

    /**
     * Converte uma planilha .xlsm/.xlsx em PDF.
     * paginas[0] recebe o número de páginas. Lança RuntimeException com a mensagem do erro.
     */
    public static native byte[] converter(byte[] planilha, String titulo, boolean ajustarLargura,
                                          boolean todasAbas, int[] paginas);

    /** Junta os PDFs na ordem dada. paginas[0] recebe o total de páginas. */
    public static native byte[] juntar(byte[][] pdfs, int[] paginas, Progresso progresso);
}
