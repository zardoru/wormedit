#pragma once

// Shared value vocabulary only; usable by frontend contracts without codecs,
// model ownership or audio/rendering services.
namespace wormedit::media {
enum class video_profile_t { mp4_h264_aac, webm_vp9_opus };
}
