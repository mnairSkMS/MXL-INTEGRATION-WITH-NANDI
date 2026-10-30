# External Encoder and Hardware Acceleration Integration

This page covers adding a third-party software encoder as a GStreamer plugin, and enabling GPU
hardware acceleration. Both describe candidate deployment steps; neither proves that a factory is
actually installed or selected at runtime. Verify with `gst-inspect-1.0` in the target environment
and inspect decoded output. See [handbook page 19](../codebase/19-gstreamer-patches-and-external-plugins.md).

## Custom software encoder

It is possible to add a custom A/V encoder as a GStreamer plugin to the Transcoder pipeline. The following steps must be followed to enable the custom encoder in the pipeline:

#### 1) Share the Plugin Binary with the Transcoder Container.

The plugin binary must be shared with the Transcoder. Additionally, an environment variable ``GST_PLUGIN_PATH`` can be set if the binary is located in a directory that is not automatically scanned by GStreamer.

#### 2) Force the Encoder in the Transcoder Configuration

The Transcoder configuration file allows you to change the default 'rank' of any GStreamer plugins. By doing this, you can easily adjust the default hierarchy of encoders, placing the custom encoder at the top of the list of available options. This feature is accessible in the ``config.json`` file at the path ``.application.gstreamer.plugins[].rank.``

Another section, ``.transcoder.outputs[].streams[].<codec>``, can be used to enforce the use of a specific encoder in the pipeline, allowing you to set its properties and capabilities in a generic (GStreamer) way.

Please refer to the configuration file documentation for further details.

The following example demonstrates how to integrate the Transcoder container with MainConcept video encoders:

##### 2.1) Force the encoder in the configuration file:

```json
{
    "application": {
        "gstreamer": {
            "plugins": [
                {
                    "name": "mcencavcdemo",
                    "rank": "MARGINAL"
                }
            ]
        }
        [...]
    },
    "transcoder": {
        [...]
        "outputs": [
            {
                [...]
                "streams": [
                    [...]
                    {
                        "type": "video",
                        "codec": "h264",
                        "video": {
                            "width": 1280,
                            "height": 720,
                            "rate": 24
                        },
                        "h264": {
                            "encoder": "mcencavcdemo",
                            "properties": {
                                "low-delay": "true"
                            },
                            "caps": {
                            }
                        }
                    }
                ]
            }
        ]
    }
}
```
##### 2.2) Add additional options to the command that starts the Transcoder container:

``-v (...)/eval_bc_delivery_gst_sdk/:/usr/local/mainconcept -e GST_PLUGIN_PATH=/usr/local/mainconcept/GSTBCDSDKDEMO-LX64_1.3.0_2023-08-23/lib/gstreamer-1.0 ``

## Hardware acceleration

The Transcoder Docker image supports hardware-accelerated video encoding through VA-API on Intel,
NVIDIA, and AMD GPUs. For background, see [GStreamer VAAPI](https://github.com/GStreamer/gstreamer-vaapi).

Add the appropriate parameters to the command that starts the Transcoder container:

| Vendor | Docker parameters |
| --- | --- |
| Intel | `--device /dev/dri:/dev/dri` |
| NVIDIA | `--gpus all --runtime nvidia -e NVIDIA_DRIVER_CAPABILITIES=video,compute,utility` |

Passing the device through does not by itself select a hardware encoder. Use the rank and explicit
`encoder` settings described above to force one, then confirm the element was actually chosen.