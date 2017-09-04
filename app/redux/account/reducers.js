// @flow

import type { ReduxAction } from '../store';
import type { BackendError } from '../../lib/backend';

export type LoginState = 'none' | 'logging in' | 'failed' | 'ok';
export type AccountReduxState = {
  accountToken: ?string,
  expiry: ?string, // ISO8601
  status: LoginState,
  error: ?BackendError
};

const initialState: AccountReduxState = {
  accountToken: null,
  expiry: null,
  status: 'none',
  error: null
};

export default function(state: AccountReduxState = initialState, action: ReduxAction): AccountReduxState {

  switch (action.type) {
  case 'LOGIN_CHANGE':
    return { ...state, ...action.newData };
  case 'START_LOGIN':
    return { ...state, ...{
      status: 'logging in',
      accountToken: action.accountToken,
      error: null,
    }};
  case 'LOGIN_SUCCESSFUL':
    return { ...state, ...{
      status: 'ok',
      error: null,
      expiry: action.expiry,
    }};
  case 'LOGIN_FAILED':
    return { ...state, ...{
      status: 'failed',
      accountToken: null,
      error: action.error,
    }};
  case 'LOGGED_OUT':
    return { ...state, ...{
      status: 'none',
      accountToken: null,
      expiry: null,
      error: null,
    }};
  case 'RESET_LOGIN_ERROR':
    return { ...state, ...{
      status: 'none',
      error: null,
    }};
  }

  return state;
}
