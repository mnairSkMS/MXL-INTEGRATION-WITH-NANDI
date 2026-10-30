# UTPL Media Transcoder Documentation

This bundle contains the following elements.

## api

Documentation of the transcoder HTTP API.

- **api-documentation.yaml** — OpenAPI source. Can be used to generate client/server stubs or
  converted to other formats. The recommended renderer is the
  [Swagger editor](https://editor.swagger.io/). The documentation is also served by the transcoder
  application on port 8080 at `http://<host>:8080/docs`, where it can be explored interactively.
- **api-documentation.html** — HTML rendering of the same source, presenting API endpoints and
  object models.

## config

Documentation of the transcoder configuration file structure.

- **config-documentation.yaml** — OpenAPI source, renderable with the
  [Swagger editor](https://editor.swagger.io/).
- **config-documentation.html** — HTML rendering presenting configuration file object models.

## misc

- **docker-deployment.md** — running the transcoder container: required files, environment
  variables, and preloading the events database.
- **encoder-integration.md** — integrating an external GStreamer encoder and enabling GPU hardware
  acceleration in the transcoder container.
- **mix-capabilities.md** — downmix/upmix capabilities when encoding to Dolby using the Dolby PAT
  libraries.

## playlist

Operator guide for playlist configuration and the local Docker Compose playlist setup.
