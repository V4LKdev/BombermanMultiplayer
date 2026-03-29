(function () {
  function buildModal() {
    const modal = document.createElement("div");
    modal.className = "image-modal";
    modal.setAttribute("role", "dialog");
    modal.setAttribute("aria-modal", "true");
    modal.setAttribute("aria-hidden", "true");

    modal.innerHTML = [
      '<div class="image-modal__backdrop" data-image-modal-close="true"></div>',
      '<figure class="image-modal__figure">',
      '<img class="image-modal__image" alt="">',
      '<figcaption class="image-modal__caption"></figcaption>',
      "</figure>",
    ].join("");

    document.body.appendChild(modal);
    return modal;
  }

  function initImageModal() {
    if (document.querySelector(".image-modal")) {
      return;
    }

    const images = document.querySelectorAll(".theme-image-swap img");
    if (!images.length) {
      return;
    }

    const modal = buildModal();
    const modalImage = modal.querySelector(".image-modal__image");
    const modalCaption = modal.querySelector(".image-modal__caption");

    function closeModal() {
      modal.classList.remove("is-open");
      modal.setAttribute("aria-hidden", "true");
      document.body.classList.remove("image-modal-open");
      modalImage.removeAttribute("src");
      modalImage.alt = "";
      modalCaption.textContent = "";
    }

    function openModal(image) {
      modalImage.src = image.currentSrc || image.src;
      modalImage.alt = image.alt || "";
      modalCaption.textContent = image.alt || "";
      modal.classList.add("is-open");
      modal.setAttribute("aria-hidden", "false");
      document.body.classList.add("image-modal-open");
    }

    images.forEach(function (image) {
      image.addEventListener("click", function () {
        openModal(image);
      });
    });

    modal.addEventListener("click", function (event) {
      if (
        event.target === modal ||
        event.target === modalImage ||
        (event.target.hasAttribute &&
          event.target.hasAttribute("data-image-modal-close"))
      ) {
        closeModal();
      }
    });

    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape" && modal.classList.contains("is-open")) {
        closeModal();
      }
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", initImageModal);
  } else {
    initImageModal();
  }
})();
