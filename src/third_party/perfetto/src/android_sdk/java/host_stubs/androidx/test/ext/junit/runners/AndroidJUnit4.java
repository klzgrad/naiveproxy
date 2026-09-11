/*
 * Copyright (C) 2026 The Android Open Source Project
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

package androidx.test.ext.junit.runners;

import org.junit.runners.BlockJUnit4ClassRunner;
import org.junit.runners.model.InitializationError;

/**
 * Host-JVM stub of androidx's runner. On Android this delegates to the
 * Robolectric / instrumentation runner depending on context; on host JUnit 4
 * is sufficient for the perfetto SDK tests.
 */
public class AndroidJUnit4 extends BlockJUnit4ClassRunner {
  public AndroidJUnit4(Class<?> klass) throws InitializationError {
    super(klass);
  }
}
