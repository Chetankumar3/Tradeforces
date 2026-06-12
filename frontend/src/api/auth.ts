import { apiClient } from './client'

export interface RegisterPayload {
  username: string
  name: string
  email: string
  password: string
}

export interface RegisterResponse {
  access_token: string
  token_type: string
  user_id: number
}

export interface LoginPayload {
  username: string
  password: string
}

export interface LoginResponse {
  access_token: string
  token_type: string
}

export async function register(payload: RegisterPayload): Promise<RegisterResponse> {
  const { data } = await apiClient.post<RegisterResponse>('/register', payload)
  return data
}

export async function login(payload: LoginPayload): Promise<LoginResponse> {
  const { data } = await apiClient.post<LoginResponse>('/login/credentials', payload)
  return data
}
