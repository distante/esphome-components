# Repository rules
- This is an ESPHome custom component repository.
- All custom components are located in the `components/` directory.
- Each component should have its own `readme.md` file.
- Contributions should follow the [ESPHome contribution guidelines](https://esphome.io/guides/contributing.html).
- Ensure that all code adheres to the [ESPHome coding standards](https://esphome.io/guides/contributing.html#coding-standards).

# Coding Rules
- We are working with C++ code for ESPHome, so please follow the C++ coding conventions https://developers.esphome.io/.
- Memory Management: Be mindful of memory usage, especially on devices with limited resources. Avoid dynamic memory allocation where possible and use static or stack allocation.
- Error Handling: Implement robust error handling to ensure that the component can gracefully handle unexpected conditions.