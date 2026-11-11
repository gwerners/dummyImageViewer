# dummyImageViewer

Visualizador de imagens simples e rápido, escrito em C++ com [raylib](https://www.raylib.com/) e [Dear ImGui](https://github.com/ocornut/imgui) (via [rlImGui](https://github.com/raylib-extras/rlImGui)). Navega por pastas com miniaturas, abre a imagem em uma janela própria e tem modo de apresentação (slideshow).

Roda em **Windows** e **Linux**. A interface está em português.

## Recursos

- Navegador de pastas com miniaturas, histórico (voltar/avançar), pasta acima e campo para digitar o caminho.
- Visualizador de imagem grande com zoom, arrastar, rotação e quatro modos de enquadramento:
  original, ajustar à altura, ajustar à largura, ajustar altura e largura.
- Apresentação (slideshow) com intervalo configurável (padrão 5 s), pausa e cancelamento.
- Segunda barra de ferramentas com girar, zoom, enquadramento, tempo e play/stop.
- Janela de ajuda com todos os atalhos (`F1`).

Rotação e enquadramento são **globais**: valem para a navegação e para a apresentação e não são salvos ao fechar o app.

### Formatos suportados

`avif`, `webp`, `jxl`, `png`, `jpg`/`jpeg`, `bmp`, `tga`, `ppm`, `pgm`, `qoi`

## Atalhos

| Contexto | Atalho | Ação |
|---|---|---|
| Pastas | `Alt+←` / `Alt+→` | Voltar / avançar no histórico |
| Pastas | `Alt+↑` / `Backspace` | Pasta acima |
| Pastas | `F5` | Recarregar pasta |
| Pastas | `Ctrl+L` | Digitar caminho |
| Visualizador | `←` / `→`, roda do mouse | Imagem anterior / próxima |
| Visualizador | `Home` / `End` | Primeira / última imagem |
| Visualizador | `1` `2` `3` `4` | Enquadramento: original / altura / largura / altura e largura |
| Visualizador | `R` / `Shift+R` | Girar para a direita / esquerda |
| Visualizador | `+` / `-` / `0` , `Ctrl+Roda` | Zoom |
| Visualizador | Botão esquerdo (arrastar) | Mover a imagem |
| Visualizador | Botão direito (clique) | Maximizar / restaurar a janela |
| Visualizador | Botão do meio (arrastar) | Mover a janela |
| Visualizador | `Esc` | Fechar o visualizador |
| Apresentação | `P` | Iniciar / parar |
| Apresentação | `Espaço` | Pausar / retomar |
| Apresentação | `Esc` | Cancelar a apresentação |
| Geral | `F1` | Mostrar / ocultar a ajuda |

## Dependências

As bibliotecas abaixo **não ficam no repositório**: os scripts de build as baixam (`git clone`) automaticamente nas pastas da raiz.

| Biblioteca | Uso |
|---|---|
| [raylib](https://github.com/raysan5/raylib) | janela, entrada e texturas |
| [Dear ImGui](https://github.com/ocornut/imgui) | interface |
| [rlImGui](https://github.com/raylib-extras/rlImGui) | integração raylib + ImGui |
| [fmt](https://github.com/fmtlib/fmt) | formatação de texto |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON |
| libavif (com dav1d), libwebp, libjxl | decodificação de AVIF, WebP e JPEG XL |

### Windows

- Visual Studio 2022 ou 2026 (carga de trabalho C++)
- CMake e git no `PATH`
- [vcpkg](https://github.com/microsoft/vcpkg): se não existir, é clonado em `C:\vcpkg` (ou use `VCPKG_ROOT`)

### Linux

- g++, cmake, ninja-build e git
- `libavif-dev`, `libwebp-dev`, `libjxl-dev`

## Compilar e executar

### Windows

```bat
build_windows.bat
```

O executável fica em `build-win\work\Release\work.exe` (as DLLs do vcpkg são copiadas junto).
Se o build falhar com `LNK1104`, feche o `work.exe` e rode de novo.

### Linux

```bash
./run.bash
```

Baixa as dependências, compila o raylib e o projeto e executa `build/work/work`.

## Limpeza

Os scripts de limpeza removem tudo o que é gerado ou baixado, deixando só o código-fonte. Use antes de commitar ou distribuir, para não inflar o repositório.

```bat
clean.bat
```
```bash
./clean.bash
```

Removem: `build`, `build-win`, `.cache`, `.vs`, `*.user`, `*.ini` e as dependências clonadas (`fmt`, `imgui`, `json`, `raylib`, `rlImGui`, `stb`). O vcpkg (`C:\vcpkg`) nunca é tocado.

> As dependências são baixadas de novo no próximo build.

## Estrutura do projeto

```
.
├── CMakeLists.txt        # raiz: adiciona raylib (Windows) e o diretório work
├── build_windows.bat     # build completo no Windows
├── run.bash              # build + execução no Linux
├── clean.bat / clean.bash# limpeza de produtos de build e dependências
├── cmake/
│   └── gwf_module.cmake  # função fix_format (formata o código-fonte no build)
└── work/                 # código do visualizador
    ├── main.cpp
    ├── Core.h / Core.cpp # interface, navegação, visualizador e apresentação
    ├── Platform.*        # partes específicas de plataforma
    ├── Task.*, Utils.*   # tarefas e utilitários
    ├── Const.h
    └── CMakeLists.txt
```

## Notas de build (Windows)

- O port `libjxl` do vcpkg não traz config CMake; o `work/CMakeLists.txt` usa `find_library`/`find_path`.
- O `libavif` precisa do `dav1d` (`libavif[dav1d]`); sem ele todo AVIF aparece preto ou falha.
- O raylib novo desliga JPG/TGA/PNM por padrão; o `CMakeLists.txt` raiz religa esses formatos.
