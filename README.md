# 💎 Traliran AI Hub

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

A lightweight, serverless, and privacy-first AI terminal client (TUI) with a built-in code execution sandbox, a dedicated AI-powered Integrated Development Environment (IDE), and multi-model parallel benchmarking. Connect directly to Groq, Gemini, OpenAI, OpenRouter, DeepSeek, Qwen, GLM, Claude, and local backends (Ollama / Llama.cpp) straight from your terminal.

---

## 🖥️ CLI / TUI Edition

This is a **native terminal client** (`traliran-hub`) written in C11 (ncurses + libcurl): chat with streaming + thinking blocks, multi-model compare, AI IDE with an on-disk workspace and version control, notes, assistant store, and settings. It connects directly to the same providers (Groq, Gemini, OpenAI, OpenRouter, DeepSeek, Qwen, GLM, Claude, Ollama, Llama.cpp).

### Dependencies
* A C11 compiler (gcc/clang) and `make`
* `ncursesw` and `libcurl` development packages
* cJSON is **vendored** at `vendor/cjson/` — no system install needed

### Build & Install
```sh
make            # builds ./traliran-hub
make install    # installs to /usr/local/bin (override with PREFIX=...)
make clean
```

### Quick Start
```sh
./traliran-hub
```
On first run the app creates `~/.cache/traliran-cache/` (config/notes/sessions stored in `storage.json`) and a starter IDE workspace at `~/.cache/traliran-cache/workspace/`.

1. Press **5** (Settings) → set your **Provider** and **API Key**, then **Fetch Models** and pick a model.
2. Return to **Chat** (press **1**) and type a message.
3. Press **?** in-app for the full keymap.

### Keymap overview
| Screen | Keys |
| --- | --- |
| Global | **1..6** switch screens (or Tab / Ctrl+1..6) · `?` help · `q` quit · **F12 toggle Vim mode** |
| Chat | Enter send · F2 new session · F3 rename · F4 delete · F5 summarize · F6 regenerate · F7 multi-model · F8 debate · F9 sandbox last code · F10 attach/remove file · Up/Down session · **PgUp/PgDn scroll messages** |
| Notes | F2 new · F3 delete · F4 AI complement · F5 export MD · F6 export to RAG · F7 import MD · F8 preview · F9 search · F10 title · **PgUp/PgDn scroll note/preview** |
| RAG | Enter send · F2 upload file (.md/.txt) · F3 delete · F4 export to Notes · Tab files/chat · Up/Down select · **PgUp/PgDn scroll chat** · F10 clear chat |
| Store | Up/Down browse · Enter install free assistant · `o` open paid link |
| IDE | F1 Files / F2 Versions / F3 Bots · F4 new file · F5 save · F9 commit · F10 revert · `x` delete · **PgUp/PgDn scroll editor** (agent panel: scrolls agent chat) · in Files list: **F1 to focus, Up/Down select, Enter open, PgUp/PgDn switch open tabs** · agent panel: Enter send, Tab switch focus |

Plain-character program hotkeys (`q`, `?`, `1..6`, Tab, IDE `x`) fire only after **3 rapid presses within 1 second**, so typing text never triggers them accidentally. F-keys and Ctrl combos fire immediately.
| Settings | Up/Down select · Enter edit · Esc back |

### Vim mode
Press **F12** to toggle vim-like navigation (shown in the status bar). In normal mode `h/j/k/l` move, `i`/`a` enter insert mode, `Esc` returns to normal mode. The setting persists in `~/.cache/traliran-cache/storage.json`.

All data stays local — no cloud sync, serverless & privacy-first.

---

## 🛡️ 100% Privacy-First & Serverless
* **Zero Middleman Servers:** This is a 100% client-side application.
* **Direct Routing:** Your API tokens are saved strictly in your local config (`~/.cache/traliran-cache/hub-config.json`) and sent straight to the official AI providers. No logs, no data collection, no leaks.
* **AGPLv3 Guaranteed:** Total freedom to audit, inspect, and host the code yourself, backed by a strong copyleft license that keeps the project forever open.

---

## 🚀 Key Features

*   **Multi-Provider Support:** Seamlessly switch between top cloud APIs (Groq, Gemini, OpenAI, OpenRouter, DeepSeek, Qwen, GLM, Claude) and local LLMs (Ollama, Llama.cpp).
*   **⚡ Multi-Model Setup & Compare:** Select multiple models simultaneously. The app triggers parallel API requests and renders side-by-side comparative cards for instant benchmarking.
*   **💡 Model Thinking Support:** Native rendering for reasoning models (like DeepSeek-R1). Structural thoughts are captured and organized into a clean, collapsible hidden dropdown block.
*   **👥 AI Group Debate Mode:** Turn your raw ideas into fully analyzed concepts. Run a multi-agent discussion loop where specialized personas (Optimist, Critic, and Technologist) cross-examine your thesis over multiple rounds.
*   **⚡ AI IDE - Integrated Development Environment:** A full-fledged terminal IDE for code generation, editing, and project management, powered by your chosen AI model.
    *   **File Explorer:** Manage multiple project files (HTML, CSS, JS, etc.).
    *   **Code Editor:** Edit files with a full-screen text editor.
    *   **Version Control:** Commit and revert snapshots of your workspace.
    *   **AI Agent:** Run a persistent coding agent in the right-hand panel.
    *   **Project Management:** Export your entire workspace as a `.tar.gz` archive.
*   **🧠 RAG Knowledge Base:** A dedicated RAG screen where you upload `.md`/`.txt` documents, then chat with the AI which retrieves the relevant files on demand via tools (system-prompt JSON protocol, same as the IDE agent). Notes export straight into the RAG knowledge base, and any RAG document can be exported back into Notes.
*   **💻 Built-in Sandbox Interpreter:** Execute, preview, and test generated HTML/JS/CSS code snippets locally without leaving the main chat workspace.
*   **🏪 Assistant Store:** Access a marketplace of free and premium, highly-optimized AI assistant presets and custom prompts for various tasks (e.g., Polyglot Translator, Code & Text Editor, Ideation Generator).
*   **⚙️ Advanced Parameters Control:** Fine-tune system behaviors with on-the-fly adjustable Temperature, Top P, and Max Tokens configuration.
*   **🌍 Full UTF-8 Input:** Type Cyrillic and other non-ASCII text everywhere (chat, notes, IDE, dialogs) with proper cursor handling.

---

## ⚖️ License & Open Source Terms

This project is licensed under the **GNU Affero General Public License v3.0 (AGPLv3)**.

### What this means for forks and deployments:
1.  **Keep it Open:** If you modify this software and run it on a server to make it accessible to other users over a network (as a SaaS or public website), you **MUST** make your modified source code available to the public under the same AGPLv3 license.
2.  **Attribution:** You must retain original copyright notices, links to this repository, and clearly state any changes made to the source code.
3.  **No Hidden Commercialization:** You cannot close the source code or hide integrated core features (like the built-in store or author credits) in your public deployments.

*For custom commercial licensing or private white-label partnerships without AGPLv3 restrictions, please contact the repository maintainer.*
