# Wormedit browser editor

Unzip the package, open a terminal in its directory and run:

```sh
python3 serve-wasm.py . --port 8000
```

Open [http://127.0.0.1:8000/](http://127.0.0.1:8000/) in a browser with WebAssembly
threads, WebGL and OffscreenCanvas support. The server supplies the isolation
headers required for WebAssembly threads.

Use **File → Save project** to download a ZIP containing the project and its
media. Reloading clears the temporary workspace.

[User manual](https://github.com/zardoru/wormedit/tree/main/docs/manual) ·
[Native releases](https://github.com/zardoru/wormedit/releases)
