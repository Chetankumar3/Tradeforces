import axios from 'axios'

const API = import.meta.env.VITE_MAIN_API_URL

// Axios instance used for all authenticated/main-API calls.
export const apiClient = axios.create({
  baseURL: API,
  headers: { 'Content-Type': 'application/json' },
})

// --- Token wiring -----------------------------------------------------------
// The token lives in React state (AuthContext). We mirror it here so the
// request interceptor can attach it without reaching into React.
let currentToken: string | null = null

export function registerAuthToken(token: string | null) {
  currentToken = token
}

apiClient.interceptors.request.use((config) => {
  if (currentToken) {
    config.headers = config.headers ?? {}
    config.headers.Authorization = `Bearer ${currentToken}`
  }
  return config
})

// --- 401 handling -----------------------------------------------------------
let unauthorizedHandler: (() => void) | null = null

export function registerUnauthorizedHandler(handler: () => void) {
  unauthorizedHandler = handler
}

apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    if (error.response?.status === 401 && unauthorizedHandler) {
      unauthorizedHandler()
    }
    return Promise.reject(error)
  },
)
