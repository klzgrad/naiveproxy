This empty folder exists to harmonize "built-in" metrics
work with the "metric extension" functionality of trace processor.

Essentially by adding a protos folder, we can pass
src/trace_processor/metrics as an extension path to shell to
override all built-in metrics. This means we can change the SQL
files *without* needing to recompile trace processor.

In the future, we might also move the protos here but that would
be a bigger change which would need coordination across repos (
e.g. Chrome because of the metrics autoroller for Chrome metrics).
