# SLL Viewer

A Geode mod for Geometry Dash that adds an in-game viewer for the Silent Level List (SLL).

Fork of ILL Viewer. It fetches the list live from `api.impossiblelevels.com` with `listType=SLL`, so no bundled data file is needed.

## Building locally

```
geode build
```

## Building on GitHub

Pushing to any branch triggers `.github/workflows/build.yml`, which builds Windows and macOS binaries via the official `geode-sdk/build-geode-mod` action and uploads them as workflow artifacts.
