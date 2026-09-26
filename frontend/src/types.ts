import type { EffectCamera } from './spatialMath'
export type StreamHealth = {
  available: boolean
  width: number | null
  height: number | null
  age_ms: number | null
}
export type Calibration = {
  required_views?: number
  elapsed_ms?: number
  last_rejection?: string | null
  status: string
  health: string
  message: string
  accepted_views: number
  rejected_views: number
  baseline_cm: number | null
  median_reprojection_error_px: number | null
  median_stereo_reprojection_error_px?: number | null
  median_pairing_error_ms: number | null
  inlier_views?: number
  outlier_views?: number
  rotation_consistency_deg?: number | null
  translation_consistency_cm?: number | null
}
export type Intrinsics = {
  elapsed_ms?: number
  last_rejection?: string | null
  profiles?: Partial<
    Record<
      'laptop' | 'phone',
      {
        available: boolean
        width: number
        height: number
        compatibility: 'missing' | 'waiting_for_camera' | 'compatible' | 'mode_mismatch'
        detail: string
      }
    >
  >
  camera_id: string
  status: string
  message: string
  accepted_views: number
  rejected_views: number
  required_views: number
  reprojection_error_px: number | null
}
export type HandTracking = {
  candidate_residual_px?: number | null
  arrival_delta_ms?: number | null
  source?: 'measured' | 'held' | 'predicted' | 'missing'
  secondary_source?: 'measured' | 'held' | 'predicted' | 'missing'
  hold_age_ms?: number | null
  secondary_hold_age_ms?: number | null
  status: string
  reason: string | null
  timing_error_ms: number | null
  confidence: number
  raw_points_m: number[][] | null
  filtered_points_m: number[][] | null
  reprojection_residuals_px: number[] | null
  landmark_confidence: number[] | null
  laptop_landmarks_normalized: number[][] | null
  phone_landmarks_normalized: number[][] | null
  secondary_raw_points_m: number[][] | null
  secondary_filtered_points_m: number[][] | null
  secondary_reprojection_residuals_px: number[] | null
  secondary_landmark_confidence: number[] | null
  secondary_confidence?: number | null
  laptop_hands_landmarks_normalized: number[][][]
  phone_hands_landmarks_normalized: number[][][]
}
export type Interaction = {
  state: string
  gesture: string | null
  gesture_confidence: number
  hold_progress: number
  hold_elapsed_ms: number
  hold_required_ms: number
  pinch: boolean
  pinch_distance_m: number | null
  pinch_reason?: string | null
  pinch_stale?: boolean
  pinch_stale_age_ms?: number | null
  pinch_threshold_m?: number
  pinch_release_threshold_m?: number
  secondary_pinch: boolean
  secondary_pinch_distance_m: number | null
  secondary_pinch_reason?: string | null
  two_hand_pinch: boolean
  two_hand_distance_m: number | null
  cursor_m: number[] | null
  object_position_m: number[]
  object_scale_m: number
  object_rotation_quat: number[]
  model_id: string
  wrist_twist_active: boolean
  object_status: string
  interaction_block_reason?: string | null
  laptop_object_corners_normalized: number[][] | null
  phone_object_corners_normalized: number[][] | null
  laptop_object_visible_edges_normalized?: number[][][] | null
  phone_object_visible_edges_normalized?: number[][][] | null
  laptop_object_points_normalized?: number[][] | null
  phone_object_points_normalized?: number[][] | null
  laptop_hand_depth_relation?: string
  phone_hand_depth_relation?: string
  detector_reason: string | null
  events: Array<{ kind: string; detail: string }>
}
export type RuntimeMetrics = {
  process_cpu_pct: number | null
  system_load_1m: number | null
  cpu_cores: number | null
  gpu_status: string
  gpu_device_utilization_pct: number | null
  gpu_renderer_utilization_pct: number | null
  gpu_memory_mb: number | null
}
export type Processing = {
  timing_basis?: string
  capture_sync_verified?: boolean
  phone_time_offset_ms?: number
  inference_cache_hits?: number
  inference_calls?: number
  processed_pairs?: number
  reason_counts?: Record<string, number>
  timing_estimate?: {
    status: 'collecting' | 'ready' | 'unreliable' | 'cancelled'
    offset_ms?: number
    correlation?: number
    baseline_correlation?: number
    reason?: string
  }
  diagnostic_counts?: Record<string, number>
  diagnostic_phase?: string
  phone_connection_event?: string
  phone_transport?: {
    packets_lost?: number
    jitter_ms?: number
    round_trip_ms?: number
    jitter_buffer_ms?: number
    clock_sync_ready?: number
    clock_probe_rtt_ms?: number
    clock_uncertainty_ms?: number
    sender_reference_packets?: number
    phone_encode_ms?: number
  }
  status: string
  calibration_file?: string
  calibration_loaded?: boolean
  calibration_storage?: string
  max_pair_error_ms?: number
  provider?: string
  execution_provider?: string
  fallback_reason?: string
  max_concurrency?: number
  error?: string
  debug_single_camera_mode?: string
  hand_max_fps?: number
  hand_frames_skipped?: number
  hand_inference_duration_ms?: number | null
  hand_inference_fps?: number | null
  hand_inference_average_ms?: number | null
  hand_inference_p95_ms?: number | null
  laptop_hands_detected?: number
  phone_hands_detected?: number
  inference_pairing_error_ms?: number | null
  inference_laptop_resolution?: number[] | null
  inference_phone_resolution?: number[] | null
  gesture_inference_duration_ms?: number | null
  runtime?: RuntimeMetrics
}
export type Telemetry = {
  cameras?: { laptop: EffectCamera; phone: EffectCamera } | null
  phone_state: string
  laptop: StreamHealth
  phone: StreamHealth
  calibration: Calibration
  intrinsics: Intrinsics
  hand_tracking: HandTracking
  interaction: Interaction
  processing: Processing
}
