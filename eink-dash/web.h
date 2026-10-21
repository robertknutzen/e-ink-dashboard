#pragma once

// Web editor on port 80 (also reachable as http://eink.local/).
//   GET  /            editor page
//   POST /save        form field "config"
//   POST /refresh     refetch sources and redraw
//   GET  /config.txt  raw config
//   POST /config.txt  raw config in the body (for curl --data-binary @file)
void webBegin();
void webLoop();
