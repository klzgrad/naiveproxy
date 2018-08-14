# Copyright 2018 The Chromium Authors. All rights reserved.
# Use of this source code is governed under the Apache License, Version 2.0
# that can be found in the LICENSE file.
"""Recipe for building GN."""

from recipe_engine.recipe_api import Property

DEPS = [
    'recipe_engine/buildbucket',
    'recipe_engine/cipd',
    'recipe_engine/context',
    'recipe_engine/file',
    'recipe_engine/json',
    'recipe_engine/path',
    'recipe_engine/platform',
    'recipe_engine/properties',
    'recipe_engine/python',
    'recipe_engine/raw_io',
    'recipe_engine/step',
    'windows_sdk',
]

PROPERTIES = {
    'repository': Property(kind=str, default='https://gn.googlesource.com/gn'),
}


def RunSteps(api, repository):
  src_dir = api.path['start_dir'].join('gn')

  with api.step.nest('git'), api.context(infra_steps=True):
    api.step('init', ['git', 'init', src_dir])

    with api.context(cwd=src_dir):
      build_input = api.buildbucket.build_input
      ref = (
          build_input.gitiles_commit.id
          if build_input.gitiles_commit else 'refs/heads/master')
      # Fetch tags so `git describe` works.
      api.step('fetch', ['git', 'fetch', '--tags', repository, ref])
      api.step('checkout', ['git', 'checkout', 'FETCH_HEAD'])
      for change in build_input.gerrit_changes:
        api.step('fetch %s/%s' % (change.change, change.patchset), [
            'git', 'fetch', repository,
            'refs/changes/%s/%s/%s' %
            (str(change.change)[-2:], change.change, change.patchset)
        ])
        api.step('cherry-pick %s/%s' % (change.change, change.patchset),
                 ['git', 'cherry-pick', 'FETCH_HEAD'])

  with api.context(infra_steps=True):
    cipd_dir = api.path['start_dir'].join('cipd')
    pkgs = api.cipd.EnsureFile()
    pkgs.add_package('infra/ninja/${platform}', 'version:1.8.2')
    if api.platform.is_linux:
      pkgs.add_package('fuchsia/clang/${platform}', 'goma')
    api.cipd.ensure(cipd_dir, pkgs)

  env = {
      'linux': {
          'CC': cipd_dir.join('bin', 'clang'),
          'CXX': cipd_dir.join('bin', 'clang++'),
          'AR': cipd_dir.join('bin', 'llvm-ar'),
          'LDFLAGS': '-static-libstdc++ -ldl -lpthread',
      },
      'mac': {},
      'win': {},
  }[api.platform.name]

  # The order is important since release build will get uploaded to CIPD.
  configs = [
      {
          'name': 'debug',
          'args': ['-d']
      },
      {
          'name': 'release',
          'args': []
      },
  ]

  for config in configs:
    with api.step.nest(config['name']):
      with api.step.nest('build'):
        with api.context(
            env=env, cwd=src_dir), api.windows_sdk(enabled=api.platform.is_win):
          api.python(
              'generate', src_dir.join('build', 'gen.py'), args=config['args'])

          # Windows requires the environment modifications when building too.
          api.step('ninja', [cipd_dir.join('ninja'), '-C', src_dir.join('out')])

      api.step('test', [src_dir.join('out', 'gn_unittests')])

  if build_input.gerrit_changes:
    return

  # TODO: Use ${platform} after crbug.com/855703 is fixed and deployed.
  platform = '%s-%s' % (api.platform.name.replace('win', 'windows'), {
      'intel': {
          32: '386',
          64: 'amd64',
      },
      'arm': {
          32: 'armv6',
          64: 'arm64',
      },
  }[api.platform.arch][api.platform.bits])

  cipd_pkg_name = 'gn/gn/' + platform
  gn = 'gn' + ('.exe' if api.platform.is_win else '')

  pkg_def = api.cipd.PackageDefinition(
      package_name=cipd_pkg_name,
      package_root=src_dir.join('out'),
      install_mode='copy')
  pkg_def.add_file(src_dir.join('out', gn))
  pkg_def.add_version_file('.versions/%s.cipd_version' % gn)

  cipd_pkg_file = api.path['cleanup'].join('gn.cipd')

  api.cipd.build_from_pkg(
      pkg_def=pkg_def,
      output_package=cipd_pkg_file,
  )

  if api.buildbucket.builder_id.project == 'infra-internal':
    with api.context(cwd=src_dir):
      revision = api.step(
          'rev-parse', ['git', 'rev-parse', 'HEAD'],
          stdout=api.raw_io.output()).stdout.strip()

    cipd_pin = api.cipd.search(cipd_pkg_name, 'git_revision:' + revision)
    if cipd_pin:
      api.step('Package is up-to-date', cmd=None)
      return

    api.cipd.register(
        package_name=cipd_pkg_name,
        package_path=cipd_pkg_file,
        refs=['latest'],
        tags={
            'git_repository': repository,
            'git_revision': revision,
        },
    )


def GenTests(api):
  REVISION = 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa'

  yield (api.test('ci') + api.platform.name('win') + api.buildbucket.ci_build(
      git_repo='gn.googlesource.com/gn',
      revision=REVISION,
  ))

  yield (api.test('cq') + api.platform.name('win') + api.buildbucket.try_build(
      gerrit_host='gn-review.googlesource.com',
      change_number=1000,
      patch_set=1,
  ))

  yield (api.test('cipd_exists') + api.buildbucket.ci_build(
      project='infra-internal',
      git_repo='gn.googlesource.com/gn',
      revision=REVISION,
  ) + api.step_data('rev-parse', api.raw_io.stream_output(REVISION)) +
         api.step_data('cipd search gn/gn/linux-amd64 git_revision:' + REVISION,
                       api.cipd.example_search('gn/gn/linux-amd64',
                                               ['git_revision:' + REVISION])))

  yield (api.test('cipd_register') + api.buildbucket.ci_build(
      project='infra-internal',
      git_repo='gn.googlesource.com/gn',
      revision=REVISION,
  ) + api.step_data('rev-parse', api.raw_io.stream_output(REVISION)) +
         api.step_data('cipd search gn/gn/linux-amd64 git_revision:' + REVISION,
                       api.cipd.example_search('gn/gn/linux-amd64', [])))
