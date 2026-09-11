// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

export = Wasm;

// WASM_ASYNC_COMPILATION=1 is set in gn/standalone/wasm.gni, so the loader
// always returns a Promise that resolves to the initialized Module.
declare function Wasm(_: Wasm.ModuleArgs): Promise<Wasm.Module>;

// See https://kripken.github.io/emscripten-site/docs/api_reference/module.html
declare namespace Wasm {
  export interface InitWasm {
    (_: ModuleArgs): Promise<Module>;
  }

  export interface FileSystemType {}

  export interface FileSystemTypes {
    MEMFS: FileSystemType;
    IDBFS: FileSystemType;
    WORKERFS: FileSystemType;
  }

  export interface FileSystemNode {
    contents: Uint8Array;
    usedBytes?: number;
  }

  export interface FileSystem {
    mkdir(path: string, mode?: number): any;
    mount(type: Wasm.FileSystemType, opts: any, mountpoint: string): any;
    unmount(mountpoint: string): void;
    unlink(mountpoint: string): void;
    lookupPath(path: string): { path: string; node: Wasm.FileSystemNode };
    filesystems: Wasm.FileSystemTypes;
  }

  export interface Module {
    callMain(args: string[]): void;
    addFunction(f: any, argTypes: string): void;
    FS_mkdir(path: string, mode?: number): any;
    FS_mount(type: Wasm.FileSystemType, opts: any, mountpoint: string): any;
    FS_lookupPath(path: string): {path: string; node: Wasm.FileSystemNode};
    FS_unlink(path: string): void;
    WORKERFS: Wasm.FileSystemType;
    ccall(
      ident: string,
      returnType: string,
      argTypes: string[],
      args: any[],
    ): number;
    HEAPU8: Uint8Array;
    FS: FileSystem;
  }

  export interface ModuleArgs {
    noInitialRun?: boolean;
    locateFile(s: string): string;
    print(s: string): void;
    printErr(s: string): void;
    onRuntimeInitialized(): void;
    onAbort?(): void;
    wasmBinary?: ArrayBuffer;
    // Optional Emscripten hook. When provided, the loader skips its own
    // fetch + compile and invokes this callback to obtain the instance.
    instantiateWasm?(
      imports: WebAssembly.Imports,
      successCallback: (
        instance: WebAssembly.Instance,
        mod: WebAssembly.Module,
      ) => void,
    ): WebAssembly.Exports;
  }
}
