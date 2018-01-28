To generate the output HTML from an input markdown file on Unix
machines, execute the command:

  PYTHONPATH=../../third_party python -m markdown -f <output>.html  <input>.md 

On Windows machines, execute:

  set PYTHONPATH=..\..\third_party
  python -m markdown -f <output>.html <input>.md

(This command line assumes that the net/docs directory is the current
directory; if that's not the case, adjust the path to src/third_party
to be accurate from whatever directory the command is executed from.)

The diagrams included in the network stack documentation were
generated with Graphviz, and both source (.dot) and output (.svg) are
included in the repository.  If graphviz is installed, the output may
be regenerated from the source via:

  dot dot -Tsvg <name>.dot > <name>.svg
