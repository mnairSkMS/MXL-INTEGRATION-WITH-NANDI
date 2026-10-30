#ifndef __APPLICATION_TYPES_H__
#define __APPLICATION_TYPES_H__

namespace app {
    // Refer to: https://www.bytescale.com/docs/image-processing-api
    enum class FitMode {
        /// @brief Resizes the image to the given dimensions, 
        /// The resulting image may be cropped in one dimension to preserve the aspect ratio of the original image.
        /// Aspect ratio preserved: yes
        /// Cropping: yes
        CROP,

        /// @brief Resizes the image to the given dimensions, stretching to fit if required 
        /// Aspect ratio preserved: no
        /// Cropping: no
        STRETCH,

        /// @brief Shrinks the image to the given dimensions but won't enlarge images that are already below the dimensions.
        /// Aspect ratio preserved: yes
        /// Cropping: no
        SHRINK,

        /// @brief Resizes the image to the given width
        /// Aspect ratio preserved: yes
        /// Cropping: no
        WIDTH,

        /// @brief Resizes the image to the given height
        /// Aspect ratio preserved: yes
        /// Cropping: no
        HEIGHT,
    };

    enum class CropMode {
        BOTTOM_LEFT,
        BOTTOM,
        BOTTOM_RIGHT,
        RIGHT,
        TOP_RIGHT,
        TOP,
        TOP_LEFT,
        LEFT,
        CENTER
    };
}

#endif /* __APPLICATION_TYPES_H__ */