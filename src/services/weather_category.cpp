#include "services/weather_category.h"

#include <climits>
#include <cstdint>

namespace services::weather {

Category deriveCategory(int32_t ceiling_ft, int visibility_sm) {
  const bool no_ceiling = (ceiling_ft == INT32_MAX);
  auto ceilCat = [&]() -> Category {
    if (no_ceiling) return Category::VFR;
    if (ceiling_ft < 500)  return Category::LIFR;
    if (ceiling_ft < 1000) return Category::IFR;
    if (ceiling_ft <= 3000) return Category::MVFR;
    return Category::VFR;
  };
  auto visCat = [&]() -> Category {
    if (visibility_sm < 1) return Category::LIFR;
    if (visibility_sm < 3) return Category::IFR;
    if (visibility_sm <= 5) return Category::MVFR;
    return Category::VFR;
  };
  const Category c1 = ceilCat();
  const Category c2 = visCat();
  return (static_cast<uint8_t>(c1) > static_cast<uint8_t>(c2)) ? c1 : c2;
}

}  // namespace services::weather
