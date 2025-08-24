project('cli', 'c',
  version : '0.0.1',
  meson_version : '>=1.6.0',
  default_options : [
    'c_std=c23',
    'werror=true',
  ]
)

# project info
project_description = 'A hierarchical CLI library for C'
project_url = 'https://github.com/smallblue2/cli'

# build options
install_headers = get_option('install_headers')

sources = [
  'src/cli.c'
]

cli_static_lib = static_library('cli',
  sources,
  install: true
)
cli_shared_lib = library('cli',
  sources,
  install: true
)

if install_headers
  install_headers('include/cli/cli.h', subdir: 'cli')
endif
