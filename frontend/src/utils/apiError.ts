import axios from 'axios'

// Extract a human-readable message from an unknown error, preferring the
// FastAPI-style `{ detail: string }` body.
export function getErrorMessage(error: unknown, fallback = 'Something went wrong'): string {
  if (axios.isAxiosError(error)) {
    const detail = error.response?.data?.detail
    if (typeof detail === 'string') return detail
    if (Array.isArray(detail) && detail[0]?.msg) return String(detail[0].msg)
    if (error.message) return error.message
  }
  if (error instanceof Error) return error.message
  return fallback
}
