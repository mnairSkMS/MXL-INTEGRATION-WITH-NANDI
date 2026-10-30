import React from 'react';

export interface InputProps extends React.InputHTMLAttributes<HTMLInputElement> {
  label?: string;
  helperText?: string;
  error?: string;
  required?: boolean;
}

export const Input = React.forwardRef<HTMLInputElement, InputProps>(
  ({ label, helperText, error, required, className = '', ...props }, ref) => {
    return (
      <div className="form-group">
        {label && (
          <label className="form-label">
            {label}
            {required && <span className="required">*</span>}
          </label>
        )}
        <input
          ref={ref}
          className={`form-input ${error ? 'error' : ''} ${className}`}
          {...props}
        />
        {helperText && !error && <div className="form-helper">{helperText}</div>}
        {error && <div className="form-error">{error}</div>}
      </div>
    );
  }
);

Input.displayName = 'Input';
