# ZanWeb.Admin.Monitor

> 源码: `packages/Zan.Mvc/src/ZanWeb/Modules/Sys/Controller/Admin/Monitor/MonitorController.zan`


## MetricWindow (class)

- long from;

- long to;

- int hours;

- bool dated;

- bool invalid;

- string day;

- string day2;


## MetricsExportDoc (class)

- long from;

- long to;

- long step_seconds;

- List<MetricsRowDoc> rows;


## MetricsRowDoc (class)

- long bucket;

- string method;

- string path;

- long calls;

- long errors;

- long avg_us;

- long max_us;

- long frames;

- long bytes;
