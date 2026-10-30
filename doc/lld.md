# UTPL - Universal Transcoding Pipeline Library - Version 1.1.0-beta.2-skandha.3
------------------------------------------------

## 1.0 **Introduction**

### **Purpose**:
This document provides a detailed overview of Skandha's UTPL (Universal Transcoding Pipeline Library). Specifically this document covers version **1.1.0-beta.2-skandha.3**. It allows future developers to begin development or maintain UTPL for it's continued operation and modification.

### **Scope**:
The modules covered in this document include

1. Transcoder
2. Transcoder API

### **Audience**:

This LLD is targeted at developers intending to start UTPL development and system maintainers wanting to debug or fix issues.

## 2. **System Context**:

UTPL is intended to run on a Ubuntu 22.04 system with Docker installed. The Docker image uses Ubuntu 24.04 as it's base image. Various Docker images for differnet functions in the system are:

1. ghcr.io/skandha-media-services/skandha-transcoder/base:1.1.0-beta.2-skandha.3
1. ghcr.io/skandha-media-services/skandha-transcoder/transcoder:1.1.0-beta.2-skandha.3
1. ghcr.io/skandha-media-services/skandha-transcoder/devel:1.1.0-beta.2-skandha.3
1. ghcr.io/skandha-media-services/skandha-transcoder/license-toolkit:1.1.0-beta.2-skandha.3
1. ghcr.io/skandha-media-services/utpl/license-server:0.1.0

Before begining development or reading this document, ensure you have access to the the **Skandha-Media-Services/media-transcoder** & **Skandha-Media-Services/skandha-transcoder** repository on GitHub and have generated a Personal Access Token (PAT) for your GitHub account. 

## 3. **Detailed Component Design**

The two high level compoenents of UTPL are 

1. Transcoder
1. Transcoder API

These are covered in the following sub-sections.

### 3.1 Transcoder

#### 3.1.1. Overview:

The transcoder process is a C++ program that uses GStreamer along with other libraries to: 
  - Take as input a media stream
  - Make alterations to it as per instructions from the user
  - Outputs a modified stream suitable for OTT playback
  
  The instructions for the modification of the media stream are given using the Transcoder API. The Transcoder API (a REST API), covered in the next section is not implemented in the Transcoder but is a separate Python program which communicates with the Transcoder using gRPC and a SQLite database (for information about events). 

#### 3.1.2. Responsibilities:

1. The responsibility of the Transcoder is to:
    - Read a configuration file (config.json)
    - Start the GStreamer pipeline with input(s) and output(s)
    - Alter the media stream as per instructions received using gRPC from the Transcoder API (a Python program).  

1. The responsibility of the Transcoder API if to:
    - Provide a HTTP interface over the network/internet
    - Accept and validate parameters
    - Communicate changes needed to the Transcoder over gRPC
    - Modify configuration file and control the Transcoder (restart)

#### 3.1.3. Dependencies:

The following libraries are used by the Trancoder

1. GStreamer (GStreamer 1.24+)
    - MainConcept
    - Visionular
    - Dolby Atmos
    - NDI
1. FFmpeg
1. SQLite
1. TSDuck
1. OpenSSL
1. Glib-2.0
1. gRPC
1. CURL
1. Boost
1. nlohmann/json
1. spdlog

#### 3.1.4. Internal Structure / Class Diagram:
  *Trancoder Class Diagram*:  
  This class diagram shows the classes, their members and wherever possible the relationship between the classes.

  Note:
  *Steps to generate the Class Diagram of the Transcoder*  
  The class diagram is generated using Mermaid (https://mermaid.js.org/)
  ```bash
  # Install Mermaid on the server
  
  # Create new Python virtual environment and install the clang
  python3 -m venv ~/clang-env
  source ~/clang-env/bin/activate
  
  pip install clang

  # Find all header files in transcoder/ and parse them with cpp_parser.py
  # This generates the classes for the class diagram that were detected from the headerfiles

  find /path/to/repository/transcoder -name "*.h" -exec python3 /path/to/repository/doc/images/cpp_parser.py {} \; > temp.mmd

  #Edit temp.mmd and check for errors
  code temp.mmd

  # Find all class names in the Transcoder
  find /path/to/repository/transcoder -name "*.h" -exec grep "class " {} \; |grep -v "Stub"|awk '{print $2}' > class_names.txt

  # Generate relationships between classes
  find /path/to/repository/transcoder -name "*.h" -exec python3 /path/to/repository/doc/images/relationships_parser.py {} \; > ../relationships.txt

  # Open relatiohnships.txt and check for errors
  code relationships.txt

  # Copy relationships in the the above file to temp.mmd as per Mermaid syntax
  ```  
  The mmd file has to be initially programatically generated using Python code to parse the header files in the repository.
  Once new classes/relationships are created/found, they have to be added to the mmd (Mermaid) file *images/lld_transcoder.mmd*.  
  The mmd file uses Mermaid syntax and the command to generate the new image is  
  ```bash
  #After installing Mermaid CLI
  mmdc -i temp.mmd  -o temp.png -t dark -b transparent --width 25000

  #Verify the files and copy to the repository
  cp temp.mmd /path/to/repository/doc/images/lld_transcoder.mmd
  cp temp.png /path/to/repository/doc/images/lld_transcoder.png
  ```

  The class diagram is searchable either by searching through the mmd file or visually using the png file.  
     
  MMD File: [LLD Transcoder MMD (Mermaid Syntax)](images/lld_transcoder.mmd)  
  Present Class Diagram: [LLD Transcoder Image](images/lld_transcoder.png)  

#### 3.1.5. Error Handling:

All the debug messages are presently printed to the console. The level of logging can be adjusted using the **level** parameter of the transcoder section of config.json.

### 3.2. Transcoder API

The API is documented on **http://(server-ip):8080/docs**. It also provides for trying out the API.  
Communication with the Transcoder takes place over gRPC.

### 3.3 Supervisor
The transcoder Docker image uses **Supervisor** to start and manage the Transcoder and Transcoder API. When the Docker image is generated from the Dockerfile, a custom supervisord.conf is copied to /etc/supervisor/ which configures starting transcoder and transcoder_api.

## 4. Database Schema

The Transcoder API and Transcoder pass data about scheduled events like
  - Insert a Logo (Image Overlay)
  - Inserting a Slate
  - Inserting a L-Band
  - SCTE35 Time Signal
  - SCTE35 Splice Insert
  - SCTE35 Return To Network

by storing them in a SQLite database. This database is created by the Transcoder API (Python program) on startup. The database is stored in **/opt/utpl/events.sqlite**. The Transcoder API is always started before the Transcoder.

The databse contains one table called **events**. Below is the schema of the table **events**

```sql
CREATE TABLE events (
        id INTEGER NOT NULL,
        type VARCHAR NOT NULL,
        time DATETIME NOT NULL,
        end_time DATETIME,
        details VARCHAR NOT NULL,
        creation_time DATETIME DEFAULT (strftime('%Y-%m-%d %H:%M:%fZ', 'now')) NOT NULL,
        read_time DATETIME,
        PRIMARY KEY (id)
);
CREATE INDEX ix_events_time ON events (time);
CREATE INDEX ix_events_end_time ON events (end_time);
CREATE INDEX ix_events_id ON events (id);
CREATE INDEX ix_events_type ON events (type);
```

## 5. **Deployment Details**

### **Pre-requisites**:

1. OS: Ubuntu 22.04 LTS
1. Docker Community Edition
    - Docker images use Ubuntu 24.04 as the base image.
1. Editor: Eg. nano or vim. Set converting **Tabs to Spaces** and **Tab Size to 4 spaces**.
1. Editior: VS Code

### **Deployment Steps**:
  1. Pull Docker images
      - docker pull ghcr.io/skandha-media-services/skandha-transcoder/base:1.1.0-beta.2-skandha.3
      - docker pull ghcr.io/skandha-media-services/skandha-transcoder/transcoder:1.1.0-beta.2-skandha.3
      - docker pull ghcr.io/skandha-media-services/skandha-transcoder/devel:1.1.0-beta.2-skandha.3
      - docker pull ghcr.io/skandha-media-services/skandha-transcoder/license-toolkit:1.1.0-beta.2-skandha.3

  1. Create the following directory structure with directories
      - shared (for license keys and licenses)
      - config (for Transcoder configuration file)
      - media (for media files needed for logos, L-Band and Slate)

  1. Running Transcoder Docker image  
      Below is an example of the docker command. You have to modify it as per your paths.  
      As a guide, the following parameters are essential
        - --net=host: Provides for networking
        - -v (path to config.json):/opt/utpl/etc/config.json  
          The config.json should always be mapped to */opt/utpl/etc/config.json*
        - -v (path to license file):/opt/utpl/etc/transcoder.lic  
          The license should always be mapped to */opt/utpl/etc/transcoder.lic*
        - -v (path to NDI SDK):/opt/ndi  
          The NDI SDK (when needed) should always be mapped to */opt/ndi*
        - docker run -it --rm --net=host -v /home/skandha/media:/media \
  -v /home/skandha/config/config.json:/opt/utpl/etc/config.json
  -v /home/skandha/shared/licenses/transcoder2.lic:/opt/utpl/etc/transcoder.lic -v /opt/ndi:/opt/ndi ghcr.io/skandha-media-services/utpl/transcoder:1.1.0-beta.2-skandha.3

## 5. License
The licensing mechanism is extensively documented in the **doc/license** directory in the the repository.

## 5. **Configuration**
The Transcoder container uses a mapped **config.json** to configure the Transcoder. The Trancoder API does not need a conifguration file.  
  
The parameters to be configured in **config.json** are *application* and *transcoder*. The *transcoder* section needs *input* and *output* sections.  
  
Refer to examples in **transcoder/config/example** to understand the various options possible.

## 6. **Logging and Monitoring**

The Transcoder and Transcoder API print messages to the console. Presently they are to be monitored by keeping an eye on the console.

## 7. **Workflow for starting development**
1. Clone the repository **media-transcoder** or **skandha-transcoder** depending on the base from which you want to start development. The **skandha-transcoder** repo contains the following new features:
   - Akamai HLS Tag support
   - AWS S3 Output
   - NDI Input
   - API for configuring config.json and controlling Transcoder
   - Documenation for the design of the Transcoder and Transcoder API
    - Note: After updates to **media-transcoder**, the patches have to be applied to **skandha-transcoder** manually. 
    ```bash
        git clone https://github.com/Skandha-Media-Services/media-transcoder.git
        git clone https://github.com/Skandha-Media-Services/skandha-transcoder.git
    ```
1. All development is to be done in the **dev** branch of the repository
    ```bash
    git checkout dev
    ```
1. Pull the Docker images from ghcr.io. (You need to access to the images)  
    ```bash
    docker pull ghcr.io/skandha-media-services/skandha-transcoder/base:1.1.0-beta.2-skandha.3
    docker pull ghcr.io/skandha-media-services/skandha-transcoder/transcoder:1.1.0-beta.2-skandha.3
    docker pull ghcr.io/skandha-media-services/skandha-transcoder/devel:1.1.0-beta.2-skandha.3
    docker pull ghcr.io/skandha-media-services/skandha-transcoder/license-toolkit:1.1.0-beta.2-skandha.3
    ```
1. Run the devel Docker image with the source repository mapped to */src*, config.json mapped to */opt/utpl/etc/config.json* and a Transcoder license mapped to */opt/utpl/etc/transcoder.lic*
    - Following is an example of the **docker run** command
    ```bash
    docker run -it --rm --network=host -v /home/skandha/media:/media \
    -v /home/skandha/config/ndi_to_srt.json:/opt/utpl/etc/config.json \
    -v /home/skandha/shared/licenses/transcoder2.lic:/opt/utpl/etc/transcoder.lic \
    -v /home/skandha/sources/media-transcoder:/src \
    -v /opt/ndi:/opt/ndi \
    ghcr.io/skandha-media-services/skandha-transcoder/devel:1.1.0-beta.2-skandha.3 \
    bash
    ```

1. Build the Transcoder (the C++ program)
    - Compile using CMake
    ```bash
    cd /src/
    mkdir build && cd build
    cmake ../transcoder
    make -j20 #or more if the CPU has more cores and resources
    make install
    ```
1. Run the transcoder
    - Command to run the transcoder
    ```bash
    #Start the Transcoder using a development license
    app/transcoder -c /opt/utpl/etc/config.json -l ../transcoder/license/dev/licenses/offline.lic
    ```
1. Run the transcoder API
    - Commands for Preparation & Run (to be done every time you run the devel container). Assuming a Docker container is already running for the Transcoder, run
    ```bash
    docker -exec -it <container_id> bash
    ```
    
    ```bash
    cd /src/api
    python3 -m venv /tmp/venv
    source /tmp/venv/bin/activate

    poetry lock --no-update
    poetry install --all-extras
    poetry run poe codegen
    poetry run poe run
    ```
1. Once the necessary changes are made and commits made to *media-transcoder* or *skandha-transcoder*, the following steps are needed to push the image to ghcr.io. (Assuming you have the necessary permissions to push).
    - Update **version.txt** to the new version number. The file should contain just the version number.
    - Update the Dockerfile in docker directory of the repository
    - These commands are meant to be run from the host computer and not the devel image.
    ```bash
    #After successful completion of this command, 4 new Docker images with the tag 'latest' will be present on the development server
    cd docker
    ./build-docker.sh 
    
    #Run this command to update the 'latest' tag to the one mentioned in the version.txt and push the images to ghcr.io
    ./build_and_push_release.sh -s
    ```

## 8. **Glossary**
- UTPL: Universal Transcoding Pipeline Library
- NDI SDK: Network Device Interface SDK (Free version used until now)
- Docker: Docker Community Edition has been used for development
- Transcoder: Refers to the C++ application that performs the transcoding from the input and provides the output
- Transcoder API: Refers to the FastAPI application that provides the HTTP interface and communicates with the Transcoder over gRPC