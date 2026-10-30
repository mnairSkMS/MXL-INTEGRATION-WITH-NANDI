## Downmix/Upmix capabilities when encoding to Dolby using Dolby PAT libraries.


&nbsp;&nbsp;&nbsp;&nbsp;The table below shows the supported configurations for upmixing/downmixing and their behavior (e.g., silence insertion on missing channels or rendering new configuration with loudness correction by Dolby PAT libraries). The horizontal header indicates the output configuration, while the vertical header indicates the input configuration.

<table>
<tr>
    <th></th>
    <th>2.0</th>
    <th>5.1</th>
    <th>5.1.2 <sub>(1)</sub></th>
    <th>5.1.4 <sub>(1)</sub></th>
    <th>7.1.2 <sub>(1)</sub></th>
    <th>7.1.4 <sub>(1)</sub></th>
    <th>9.1.6 <sub>(1)</sub></th>
</tr>
<tr>
    <th>2.0</th>
    <td>pass-through</td>
    <td>upmix <sub>2</sub></td>
    <td>N/A</td>
    <td>upmix <sub>2</sub></td>
    <td>N/A</td>
    <td>upmix <sub>3</sub></td>
    <td>upmix <sub>3</sub></td>
</tr>
<tr>
    <th>5.1</th>
    <td>downmix <sub>2</sub></td>
    <td>pass-through</td>
    <td>N/A</td>
    <td>upmix <sub>2</sub></td>
    <td>N/A</td>
    <td>upmix <sub>3</sub></td>
    <td>upmix <sub>3</sub></td>
</tr>
<tr>
    <th>5.1.2 <sub>(1)</sub></th>
    <td>downmix <sub>2</sub></td>
    <td>downmix <sub>2</sub></td>
    <td>N/A</td>
    <td>upmix <sub>2, 4</sub></td>
    <td>N/A</td>
    <td>upmix <sub>3, 4</sub></td>
    <td>upmix <sub>3, 4</sub></td>
</tr>
<tr>
    <th>5.1.4 <sub>(1)</sub></th>
    <td>downmix <sub>2</sub></td>
    <td>downmix <sub>2</sub></td>
    <td>N/A</td>
    <td>pass-through <sub>4</sub></td>
    <td>N/A</td>
    <td>upmix <sub>3, 4</sub></td>
    <td>upmix <sub>3, 4</sub></td>
</tr>
<tr>
    <th>7.1.2 <sub>(1)</sub></th>
    <td>N/A</td>
    <td>N/A</td>
    <td>N/A</td>
    <td>N/A</td>
    <td>N/A</td>
    <td>N/A</td>
    <td>N/A</td>
</tr>
<tr>
    <th>7.1.4 <sub>(1)</sub></th>
    <td>downmix <sub>2</sub></td>
    <td>downmix <sub>2</sub></td>
    <td>N/A</td>
    <td>downmix <sub>2, 4</sub></td>
    <td>N/A</td>
    <td>pass-through <sub>4</sub></td>
    <td>upmix <sub>3, 4</sub></td>
</tr>
<tr>
    <th>9.1.6 <sub>(1)</sub></th>
    <td>downmix <sub>2</sub></td>
    <td>downmix <sub>2</sub></td>
    <td>N/A</td>
    <td>downmix <sub>2, 4</sub></td>
    <td>N/A</td>
    <td>N/A</td>
    <td>pass-through <sub>4</sub></td>
</tr>
</table>

Where:
  * <sub>1</sub> - Dolby Digital Plus + Atmos.
  * <sub>2</sub> - Upmix using Dolby PAT Renderer library (PADMX).
  * <sub>3</sub> - Upmix where all additional channels, relative to the input, are filled with silence. Upmix operation is done directly by Dolby PAT encoder library (PAEC)
  * <sub>4</sub> - Atmos metadata from the input are cropped because the Dolby PAT decoder (PADC) performs a downmix to 5.1 by default. Please refer to the following note from the documentation: 
    > For transcoding from Dolby Digital/Dolby Digital Plus, the decoder output ( speaker_config )
needs to be configured to 5.1 for all cases except for a 2.0 final output, in which case the
decoder output is better set to 2.0 output as well. Although the decoder supports other output
configurations such as 7.1, 5.1.2, 5.1.4, and 7.1.4, these transcoding use cases are not
supported in the kit. In particular, trying to handle the full channel configuration of any
possible input bitstream configurations ( acmod ) by setting the decoder output to 5.1.4 or 7.1.4
is an incorrect approach.