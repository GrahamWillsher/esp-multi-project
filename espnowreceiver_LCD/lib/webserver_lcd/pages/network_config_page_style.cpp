#include "network_config_page_style.h"

String get_network_config_page_style() {
    return R"rawliteral(
        .form-control {
            max-width: 250px;
            padding: 0.5rem;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-size: 1rem;
            box-sizing: border-box;
        }
        
        .form-control:focus {
            outline: none;
            border-color: #007bff;
            box-shadow: 0 0 0 3px rgba(0, 123, 255, 0.1);
        }
        
        .form-help {
            display: block;
            margin-top: 0.25rem;
            font-size: 0.875rem;
            color: #666;
        }
        
        .required {
            color: #dc3545;
        }
        
        /* IP row and octet styles removed - using common_styles.h definitions */
        
        .button-group {
            display: flex;
            gap: 1rem;
            margin-top: 2rem;
            justify-content: center;
        }
        
        .btn {
            padding: 0.75rem 1.5rem;
            font-size: 1rem;
            font-weight: 600;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            transition: background-color 0.2s;
            min-width: 150px;
        }
        
        .btn-primary {
            background-color: #4CAF50 !important;
            color: white !important;
        }
        
        .btn-primary:hover {
            background-color: #45a049 !important;
        }
        
        .btn-secondary {
            background-color: #6c757d !important;
            color: white !important;
        }
        
        .btn-secondary:hover {
            background-color: #545b62 !important;
        }
        
        .alert {
            padding: 1rem;
            border-radius: 4px;
            margin-bottom: 1.5rem;
        }
        
        .alert-warning {
            background-color: #fff3cd;
            border: 1px solid #ffc107;
            color: #856404;
        }
        
        .alert-info {
            background-color: #d1ecf1;
            border: 1px solid #17a2b8;
            color: #0c5460;
        }
        
        /* Editable field styling - right-aligned text */
        .editable-field {
            text-align: right;
        }
        
        /* MAC address monospace font */
        .editable-mac {
            font-family: 'Courier New', Courier, monospace;
        }
        
        /* Enhanced checkbox row highlight */
        .checkbox-row {
            display: flex;
            align-items: center;
            gap: 0.5rem;
            padding: 0.75rem;
            border-radius: 4px;
            background-color: #f8f9fa;
            border: 2px solid #e9ecef;
            transition: all 0.2s ease;
        }
        
        .checkbox-row:hover {
            background-color: #e9ecef;
            border-color: #007bff;
        }
        
        .checkbox-row input[type="checkbox"] {
            width: 20px;
            height: 20px;
            cursor: pointer;
        }
        
        .checkbox-row label {
            margin: 0;
            cursor: pointer;
            font-weight: 500;
        }
)rawliteral";
}