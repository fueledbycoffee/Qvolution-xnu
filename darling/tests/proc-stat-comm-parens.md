# proc-stat-comm-parens

Regression test for `/proc/<pid>/stat` parser `next_stat_elem()` handling of the `comm` field.

Linux `proc(5)` specifies that the second field in `/proc/<pid>/stat` is the process name (`comm`) enclosed in parentheses. The process name itself may contain whitespace and parentheses (such as `(a) b)` or `(Web Content (Pre-rendered))`).

The parser must locate the closing parenthesis via the last `)` in the field rather than the first `)`. If no closing parenthesis is found, the parser must return `NULL` and not scan past the end of the buffer.

Run on the host:

```bash
ruby darling/tests/proc-stat-comm-parens.rb
```
