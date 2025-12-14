/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/websocket_bridge/websocket_bridge.h"

// This is split in a dedicated translation unit so that tracebox can refer to
// WebsocketBridgeMain() without pulling in a main() symbol.
int main(int argc, char** argv) {
  return perfetto::WebsocketBridgeMain(argc, argv);
}
