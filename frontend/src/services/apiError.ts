/**
 * Normalized API Error for the Skandha transcoder management API
 * Handles both this API's application errors {"message": "...", "code": "..."}
 * and FastAPI standard exceptions {"detail": "..."}.
 */
export class ApiError extends Error {
  public status: number;
  public code?: string;
  public detail?: string | object;

  constructor(status: number, message: string, code?: string, detail?: string | object) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
    this.detail = detail;
  }

  static async fromResponse(response: Response): Promise<ApiError> {
    const status = response.status;
    let message = `Request failed with status ${status}`;
    let code: string | undefined;
    let detail: string | object | undefined;

    try {
      const data = await response.json();
      if (typeof data === 'object' && data !== null) {
        if ('message' in data && typeof data.message === 'string') {
          message = data.message;
        } else if ('detail' in data) {
          if (typeof data.detail === 'string') {
            message = data.detail;
          } else {
            message = JSON.stringify(data.detail);
          }
          detail = data.detail;
        }

        if ('code' in data && typeof data.code === 'string') {
          code = data.code;
        }
      }
    } catch {
      // Body was not JSON
    }

    // Contextual status overrides per API-CREATE-CHANNEL.md
    if (status === 404 && !message) {
      message = 'Configuration not found (unconfigured appliance)';
    } else if (status === 409) {
      message = message || 'Configuration path or destination conflict';
    } else if (status === 422) {
      message = message || 'Structural validation error in configuration';
    }

    return new ApiError(status, message, code, detail);
  }
}
