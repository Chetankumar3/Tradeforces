import axios from 'axios'
import type { AxiosProgressEvent } from 'axios'
import { apiClient } from './client'

export interface PresignedUrlResponse {
  presigned_url: string
  submission_id: number
}

export interface UploadCompleteResponse {
  status: string
  message: string
}

// Step 1 — Request a presigned URL using a client-generated integer id.
export async function getPresignedUrl(
  clientSubmissionId: number,
): Promise<PresignedUrlResponse> {
  const { data } = await apiClient.get<PresignedUrlResponse>(
    `/submit/${clientSubmissionId}`,
  )
  return data
}

// Step 2 — Upload the file/blob directly to GCS via the presigned URL.
// IMPORTANT: no Authorization header on this request.
export async function uploadToGCS(
  presignedUrl: string,
  file: File | Blob,
  onProgress?: (percent: number) => void,
): Promise<void> {
  await axios.put(presignedUrl, file, {
    headers: { 'Content-Type': 'application/zip' },
    // Use a bare axios call (not apiClient) so the auth interceptor never runs.
    transformRequest: [(d) => d],
    onUploadProgress: (event: AxiosProgressEvent) => {
      if (!onProgress) return
      const total = event.total ?? 0
      if (total > 0) {
        onProgress(Math.round((event.loaded / total) * 100))
      }
    },
  })
}

// Step 3 — Notify the backend that the upload finished.
export async function notifyUploadComplete(
  submissionId: number,
): Promise<UploadCompleteResponse> {
  const { data } = await apiClient.post<UploadCompleteResponse>(
    `/upload_complete/${submissionId}`,
  )
  return data
}
