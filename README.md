webbench
========
Web Bench is very simple tool for benchmarking WWW or proxy servers. 
Uses fork() for simulating multiple clients and can use HTTP/0.9-HTTP/1.1 requests. 
This benchmark is not very realistic, but it can test if your HTTPD can realy handle
that many clients at once (try to run some CGIs) without taking your machine down. 
Displays pages/min and bytes/sec. Can be used in more aggressive mode with -f switch.
