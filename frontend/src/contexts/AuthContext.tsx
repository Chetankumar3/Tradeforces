import {
  createContext,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from 'react'
import { useNavigate } from 'react-router-dom'
import { registerAuthToken, registerUnauthorizedHandler } from '../api/client'

const TOKEN_KEY = 'tradeforces_token'
const USER_ID_KEY = 'tradeforces_user_id'

interface AuthContextValue {
  token: string | null
  userId: number | null
  isAuthenticated: boolean
  login: (token: string, userId?: number | null) => void
  logout: () => void
}

const AuthContext = createContext<AuthContextValue | undefined>(undefined)

function readStoredUserId(): number | null {
  const raw = localStorage.getItem(USER_ID_KEY)
  if (raw === null) return null
  const parsed = Number(raw)
  return Number.isFinite(parsed) ? parsed : null
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const navigate = useNavigate()
  const [token, setToken] = useState<string | null>(() =>
    localStorage.getItem(TOKEN_KEY),
  )
  const [userId, setUserId] = useState<number | null>(() => readStoredUserId())

  // Keep the axios interceptor in sync with the current token.
  useEffect(() => {
    registerAuthToken(token)
  }, [token])

  const login = (newToken: string, newUserId?: number | null) => {
    localStorage.setItem(TOKEN_KEY, newToken)
    setToken(newToken)
    if (newUserId !== undefined && newUserId !== null) {
      localStorage.setItem(USER_ID_KEY, String(newUserId))
      setUserId(newUserId)
    }
    registerAuthToken(newToken)
  }

  const logout = () => {
    localStorage.removeItem(TOKEN_KEY)
    localStorage.removeItem(USER_ID_KEY)
    setToken(null)
    setUserId(null)
    registerAuthToken(null)
    navigate('/login', { replace: true })
  }

  // Let the axios 401 handler trigger a logout from outside React.
  useEffect(() => {
    registerUnauthorizedHandler(() => {
      localStorage.removeItem(TOKEN_KEY)
      localStorage.removeItem(USER_ID_KEY)
      setToken(null)
      setUserId(null)
      registerAuthToken(null)
      navigate('/login', { replace: true })
    })
  }, [navigate])

  const value = useMemo<AuthContextValue>(
    () => ({
      token,
      userId,
      isAuthenticated: Boolean(token),
      login,
      logout,
    }),
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [token, userId],
  )

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>
}

export function useAuth(): AuthContextValue {
  const ctx = useContext(AuthContext)
  if (!ctx) throw new Error('useAuth must be used within an AuthProvider')
  return ctx
}
