# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Implements commands for running and interacting with Fuchsia on AEMU."""

import os
import platform
import qemu_target
import logging

from common import GetEmuRootForPlatform

class AemuTarget(qemu_target.QemuTarget):
  def __init__(self, output_dir, target_cpu, system_log_file, emu_type,
               cpu_cores, require_kvm, ram_size_mb, enable_graphics,
               hardware_gpu):
    super(AemuTarget, self).__init__(output_dir, target_cpu, system_log_file,
                                     emu_type, cpu_cores, require_kvm,
                                     ram_size_mb)

    # TODO(crbug.com/1000907): Enable AEMU for arm64.
    if platform.machine() == 'aarch64':
      raise Exception('AEMU does not support arm64 hosts.')
    self._enable_graphics = enable_graphics
    self._hardware_gpu = hardware_gpu

  def _EnsureEmulatorExists(self, path):
    assert os.path.exists(path), \
          'This checkout is missing %s.' % (self._emu_type)

  def _BuildCommand(self):
    aemu_folder = GetEmuRootForPlatform(self._emu_type)

    self._EnsureEmulatorExists(aemu_folder)
    aemu_path = os.path.join(aemu_folder, 'emulator')

    # `VirtioInput` is needed for touch input device support on Fuchsia.
    # `RefCountPipe` is needed for proper cleanup of resources when a process
    # that uses Vulkan dies inside the guest
    aemu_features = 'VirtioInput,RefCountPipe'

    # Configure the CPU to emulate.
    # On Linux, we can enable lightweight virtualization (KVM) if the host and
    # guest architectures are the same.
    if self._IsKvmEnabled():
      aemu_features += ',KVM,GLDirectMem,Vulkan'
    else:
      if self._target_cpu != 'arm64':
        aemu_features += ',-GLDirectMem'

    # Use Swiftshader for Vulkan if requested
    gpu_target = 'swiftshader_indirect'
    if self._hardware_gpu:
      gpu_target = 'host'

    aemu_command = [aemu_path]
    if not self._enable_graphics:
      aemu_command.append('-no-window')
    # All args after -fuchsia flag gets passed to QEMU
    aemu_command.extend([
        '-feature', aemu_features, '-window-size', '1024x600', '-gpu',
        gpu_target, '-fuchsia'
    ])

    aemu_command.extend(self._BuildQemuConfig())

    aemu_command.extend([
      '-vga', 'none',
      '-device', 'isa-debug-exit,iobase=0xf4,iosize=0x04',
      '-device', 'virtio-keyboard-pci',
      '-device', 'virtio_input_multi_touch_pci_1',
      '-device', 'ich9-ahci,id=ahci'])
    logging.info(' '.join(aemu_command))
    return aemu_command
